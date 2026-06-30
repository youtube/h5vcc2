// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Modifications Copyright 2017 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package dev.cobalt.media;

import static dev.cobalt.media.Log.TAG;

import android.media.DeniedByServerException;
import android.media.MediaCrypto;
import android.media.MediaCryptoException;
import android.media.MediaDrm;
import android.media.MediaDrm.OnEventListener;
import android.media.MediaDrmException;
import android.media.NotProvisionedException;
import android.media.UnsupportedSchemeException;
import android.os.Build;
import android.util.Base64;
import androidx.annotation.RequiresApi;
import dev.cobalt.coat.CobaltHttpHelper;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.UUID;
import org.chromium.base.Log;
import org.jni_zero.CalledByNative;
import org.jni_zero.CalledByNativeUnchecked;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/** A wrapper of the android MediaDrm class. */
@JNINamespace("starboard")
public class MediaDrmBridge {
  // Implementation Notes:
  // - A media crypto session (mMediaCryptoSession) is opened after MediaDrm
  //   is created. This session will NOT be added to mSessionIds and will only
  //   be used to create the MediaCrypto object.
  // - Each createSession() call creates a new session. All created sessions
  //   are managed in mSessionIds.
  // - Whenever NotProvisionedException is thrown, we will clean up the
  //   current state and start the provisioning process.
  // - When provisioning is finished, we will try to resume suspended
  //   operations:
  //   a) Create the media crypto session if it's not created.
  //   b) Finish createSession() if previous createSession() was interrupted
  //      by a NotProvisionedException.
  // - Whenever an unexpected error occurred, we'll call release() to release
  //   all resources immediately, clear all states and fail all pending
  //   operations. After that all calls to this object will fail (e.g. return
  //   null or reject the promise). All public APIs and callbacks should check
  //   mMediaBridge to make sure release() hasn't been called.

  private static final char[] HEX_CHAR_LOOKUP = "0123456789ABCDEF".toCharArray();
  private static final long INVALID_NATIVE_MEDIA_DRM_BRIDGE = 0;

  // The value of this must stay in sync with kSbDrmTicketInvalid in "starboard/drm.h"
  private static final int SB_DRM_TICKET_INVALID = Integer.MIN_VALUE;

  // Scheme UUID for Widevine. See http://dashif.org/identifiers/protection/
  private static final UUID WIDEVINE_UUID = UUID.fromString("edef8ba9-79d6-4ace-a3c8-27dcd51d21ed");

  // Deprecated in API 26, but we still log it on earlier devices.
  // We do handle STATUS_EXPIRED in onKeyStatusChange() for API 23+ devices.
  @SuppressWarnings("deprecation")
  private static final int MEDIA_DRM_EVENT_KEY_EXPIRED = MediaDrm.EVENT_KEY_EXPIRED;

  // Deprecated in API 23, but we still log it on earlier devices.
  @SuppressWarnings("deprecation")
  private static final int MEDIA_DRM_EVENT_PROVISION_REQUIRED = MediaDrm.EVENT_PROVISION_REQUIRED;

  // Added in API 23.
  private static final int MEDIA_DRM_EVENT_SESSION_RECLAIMED = MediaDrm.EVENT_SESSION_RECLAIMED;

  private MediaDrm mMediaDrm;
  private long mNativeMediaDrmBridge;
  private final UUID mSchemeUUID;
  private final boolean mEnableAppProvisioning;

  // A session only for the purpose of creating a MediaCrypto object. Created
  // after construction, or after the provisioning process is successfully
  // completed. No getKeyRequest() should be called on |mMediaCryptoSession|.
  private byte[] mMediaCryptoSession;

  // The map of all opened sessions (excluding mMediaCryptoSession) to their
  // mime types.
  private HashMap<ByteBuffer, String> mSessionIds = new HashMap<>();

  private MediaCrypto mMediaCrypto;

  // Return value for DRM operation that MediaDrmBridge executes.
  private static class OperationResult {
    private final @DrmOperationStatus int mStatus;

    // Descriptive error message or details, in the scenario where the update session call failed.
    private final String mErrorMessage;

    private OperationResult(@DrmOperationStatus int status, String errorMessage) {
      this.mStatus = status;
      this.mErrorMessage = errorMessage;
    }

    public static OperationResult success() {
      return new OperationResult(DrmOperationStatus.SUCCESS, "");
    }

    public static OperationResult operationFailed(String errorMessage) {
      return new OperationResult(DrmOperationStatus.OPERATION_FAILED, errorMessage);
    }

    public static OperationResult operationFailed(String errorMessage, Throwable e) {
      return operationFailed(String.format("%s StackTrace: %s", errorMessage, Log.getStackTraceString(e)));
    }

    public static OperationResult notProvisioned(Throwable e) {
      return new OperationResult(DrmOperationStatus.NOT_PROVISIONED, String.format("Device is not provisioned. StackTrace: %s", Log.getStackTraceString(e)));
    }

    @CalledByNative("OperationResult")
    public String getErrorMessage() {
      return mErrorMessage;
    }

    @CalledByNative("OperationResult")
    public int getStatusCode() {
      return mStatus;
    }

    public boolean isSuccess() {
      return mStatus == DrmOperationStatus.SUCCESS;
    }
  }

  /**
   * Create a new MediaDrmBridge with the Widevine crypto scheme.
   *
   * @param nativeMediaDrmBridge The native owner of this class.
   */
  @CalledByNative
  static MediaDrmBridge create(String keySystem, boolean enableAppProvisioning, long nativeMediaDrmBridge) {
    UUID cryptoScheme = WIDEVINE_UUID;
    if (!MediaDrm.isCryptoSchemeSupported(cryptoScheme)) {
      return null;
    }

    MediaDrmBridge mediaDrmBridge = null;
    try {
      mediaDrmBridge = new MediaDrmBridge(keySystem, cryptoScheme, enableAppProvisioning, nativeMediaDrmBridge);
      Log.d(TAG, "MediaDrmBridge successfully created.");
    } catch (UnsupportedSchemeException e) {
      Log.e(TAG, "Unsupported DRM scheme", e);
      return null;
    } catch (IllegalArgumentException e) {
      Log.e(TAG, "Failed to create MediaDrmBridge", e);
      return null;
    } catch (IllegalStateException e) {
      Log.e(TAG, "Failed to create MediaDrmBridge", e);
      return null;
    }

    if (!mediaDrmBridge.createMediaCrypto()) {
      return null;
    }

    return mediaDrmBridge;
  }

  /**
   * Check whether the Widevine crypto scheme is supported.
   *
   * @return true if the container and the crypto scheme is supported, or false otherwise.
   */
  @CalledByNative
  static boolean isWidevineCryptoSchemeSupported() {
    return MediaDrm.isCryptoSchemeSupported(WIDEVINE_UUID);
  }

  /**
   * Check whether `cbcs` scheme is supported.
   *
   * @return true if the `cbcs` encryption is supported, or false otherwise.
   */
  @CalledByNative
  static boolean isCbcsSchemeSupported() {
    // While 'cbcs' scheme was originally implemented in N, there was a bug (in the
    // DRM code) which means that it didn't really work properly until N-MR1).
    return Build.VERSION.SDK_INT >= Build.VERSION_CODES.N_MR1;
  }

  /** Destroy the MediaDrmBridge object. */
  @CalledByNativeUnchecked
  void destroy() {
    mNativeMediaDrmBridge = INVALID_NATIVE_MEDIA_DRM_BRIDGE;
    if (mMediaDrm != null) {
      release();
    }
  }

  @CalledByNative
  void createSession(int ticket, byte[] initData, String mime) {
    Log.d(TAG, "createSession()");

    if (mMediaDrm == null) {
      Log.e(TAG, "createSession() called when MediaDrm is null.");
      return;
    }

    boolean newSessionOpened = false;
    byte[] sessionId = null;
    try {
      sessionId = openSession();
      if (sessionId == null) {
        Log.e(TAG, "Open session failed.");
        return;
      }
      newSessionOpened = true;
      if (sessionExists(sessionId)) {
        Log.e(TAG, "Opened session that already exists.");
        return;
      }

      MediaDrm.KeyRequest request = null;
      request = getKeyRequest(sessionId, initData, mime);
      if (request == null) {
        closeMediaDrmSession(sessionId);
        Log.e(TAG, "Generate request failed.");
        return;
      }

      // Success!
      Log.d(TAG, "Session is created: sessionId=" + bytesToString(sessionId));
      mSessionIds.put(ByteBuffer.wrap(sessionId), mime);
      onSessionMessage(ticket, sessionId, request);
    } catch (NotProvisionedException e) {
      Log.e(TAG, "Device not provisioned", e);
      if (newSessionOpened) {
        closeMediaDrmSession(sessionId);
      }
      attemptProvisioning();
    }
  }

  @CalledByNative
  OperationResult createSessionWithAppProvisioning(int ticket, byte[] initData, String mime) {
    assert mEnableAppProvisioning;
    if (mMediaDrm == null) {
      Log.e(TAG, "createSessionWithAppProvisioning() called when MediaDrm is null.");
      return OperationResult.operationFailed("createSessionWithAppProvisioning() called when MediaDrm is null.");
    }

    OperationResult result = createMediaCryptoSessionWithAppProvisioning();
    if (!result.isSuccess()) {
      return result;
    }

    byte[] sessionId;
    try {
      sessionId = openSession();
    } catch (NotProvisionedException e) {
      Log.e(TAG, "openSession failed: Device not provisioned", e);
      return OperationResult.notProvisioned(e);
    }
    if (sessionId == null) {
      Log.e(TAG, "Open session failed.");
      return OperationResult.operationFailed("Open session failed");
    } else if (sessionExists(sessionId)) {
      Log.e(TAG, "Opened session that already exists.");
      return OperationResult.operationFailed("Opened session that already exists");
    }

    MediaDrm.KeyRequest request;
    try {
      request = getKeyRequest(sessionId, initData, mime);
    } catch (NotProvisionedException e) {
      Log.e(TAG, "getKeyRequest failed, since device not provisioned", e);
      closeMediaDrmSession(sessionId);
      return OperationResult.notProvisioned(e);
    } catch (Exception e) {
      Log.e(TAG, "getKeyRequest failed", e);
      closeMediaDrmSession(sessionId);
      return OperationResult.operationFailed("getKeyRequestf failed", e);
    }
    if (request == null) {
      closeMediaDrmSession(sessionId);
      Log.e(TAG, "Generate request failed.");
      return OperationResult.operationFailed("Generate request failed");
    }

    Log.d(TAG, "Session is created: sessionId=%s", bytesToString(sessionId));
    mSessionIds.put(ByteBuffer.wrap(sessionId), mime);
    onSessionMessage(ticket, sessionId, request);

    return OperationResult.success();
  }

  /**
   * Update a session with response.
   *
   * @param sessionId Reference ID of session to be updated.
   * @param response Response data from the server.
   */
  @CalledByNative
  OperationResult updateSession(int ticket, byte[] sessionId, byte[] response) {
    Log.d(TAG, "updateSession()");
    if (mMediaDrm == null) {
      Log.e(TAG, "updateSession() called when MediaDrm is null.");
      return OperationResult.operationFailed(
          "Null MediaDrm object when calling updateSession().");
    }

    if (!sessionExists(sessionId)) {
      Log.e(TAG, "updateSession tried to update a session that does not exist.");
      return OperationResult.operationFailed(
          "Failed to update session because it does not exist.");
    }

    try {
      try {
        mMediaDrm.provideKeyResponse(sessionId, response);
      } catch (IllegalStateException e) {
        // This is not really an exception. Some error codes are incorrectly
        // reported as an exception.
        Log.e(TAG, "Exception intentionally caught when calling provideKeyResponse()", e);
      }
      Log.d(TAG, "Key successfully added for sessionId=" + bytesToString(sessionId));
      return OperationResult.success();
    } catch (NotProvisionedException e) {
      // TODO: Should we handle this?
      Log.e(TAG, "Failed to provide key response", e);
      release();
      return OperationResult.operationFailed(
          "Update session failed due to lack of provisioning.", e);
    } catch (DeniedByServerException e) {
      Log.e(TAG, "Failed to provide key response.", e);
      release();
      return OperationResult.operationFailed(
          "Update session failed because we were denied by server.", e);
    } catch (Exception e) {
      Log.e(TAG, "", e);
      release();
      return OperationResult.operationFailed(
          "Update session failed. Caught exception: " + e.getMessage(), e);
    }
  }

  /**
   * Close a session that was previously created by createSession().
   *
   * @param sessionId ID of session to be closed.
   */
  @CalledByNativeUnchecked
  void closeSession(byte[] sessionId) {
    Log.d(TAG, "closeSession()");
    if (mMediaDrm == null) {
      Log.e(TAG, "closeSession() called when MediaDrm is null.");
      return;
    }

    if (!sessionExists(sessionId)) {
      Log.e(TAG, "Invalid sessionId in closeSession(): sessionId=" + bytesToString(sessionId));
      return;
    }

    try {
      // Some implementations don't have removeKeys.
      // https://bugs.chromium.org/p/chromium/issues/detail?id=475632
      mMediaDrm.removeKeys(sessionId);
    } catch (Exception e) {
      Log.e(TAG, "removeKeys failed: ", e);
    }

    closeMediaDrmSession(sessionId);

    mSessionIds.remove(ByteBuffer.wrap(sessionId));
    Log.d(TAG, "Session closed: sessionId=" + bytesToString(sessionId));
  }

  @CalledByNative
  byte[] getMetricsInBase64() {
    if (Build.VERSION.SDK_INT < 28) {
      return null;
    }
    byte[] metrics;
    try {
      metrics = mMediaDrm.getPropertyByteArray("metrics");
    } catch (Exception e) {
      Log.e(TAG, "Failed to retrieve DRM Metrics.");
      return null;
    }
    return Base64.encode(metrics, Base64.NO_PADDING | Base64.NO_WRAP | Base64.URL_SAFE);
  }

  @CalledByNative
  MediaCrypto getMediaCrypto() {
    return mMediaCrypto;
  }

  private MediaDrmBridge(String keySystem, UUID schemeUUID, boolean enableAppProvisioning, long nativeMediaDrmBridge)
      throws android.media.UnsupportedSchemeException {
    mSchemeUUID = schemeUUID;
    mMediaDrm = new MediaDrm(schemeUUID);
    mEnableAppProvisioning = enableAppProvisioning;

    // Get info of hdcp connection
    if (Build.VERSION.SDK_INT >= 29) {
      getConnectedHdcpLevelInfoV29(mMediaDrm);
    }

    mNativeMediaDrmBridge = nativeMediaDrmBridge;
    if (!isNativeMediaDrmBridgeValid()) {
      throw new IllegalArgumentException(
          String.format("Invalid nativeMediaDrmBridge value: |%d|.", nativeMediaDrmBridge));
    }

    mMediaDrm.setOnEventListener(
        new OnEventListener() {
          @Override
          public void onEvent(MediaDrm md, byte[] sessionId, int event, int extra, byte[] data) {
            if (event == MediaDrm.EVENT_KEY_REQUIRED) {
              Log.d(TAG, "MediaDrm.EVENT_KEY_REQUIRED");
              if (mEnableAppProvisioning) {
                handleKeyRequiredEventWithAppProvisioning(sessionId, data);
                return;
              }

              if (sessionId == null) {
                Log.e(TAG, "EventListener: Null session.");
                return;
              }
              if (!sessionExists(sessionId)) {
                Log.e(TAG, "EventListener: Invalid session id=" + bytesToString(sessionId));
                return;
              }

              String mime = mSessionIds.get(ByteBuffer.wrap(sessionId));
              MediaDrm.KeyRequest request = null;
              try {
                request = getKeyRequest(sessionId, data, mime);
              } catch (NotProvisionedException e) {
                Log.e(TAG, "Device not provisioned", e);
                if (!attemptProvisioning()) {
                  Log.e(TAG, "Failed to provision device when responding to EVENT_KEY_REQUIRED");
                  return;
                }
                // If we supposedly successfully provisioned ourselves, then try to create a
                // request again.
                try {
                  request = getKeyRequest(sessionId, data, mime);
                } catch (NotProvisionedException e2) {
                  Log.e(
                      TAG,
                      "Device still not provisioned after supposedly successful provisioning",
                      e2);
                  return;
                }
              }
              if (request != null) {
                onSessionMessage(SB_DRM_TICKET_INVALID, sessionId, request);
              } else {
                Log.e(TAG, "EventListener: getKeyRequest failed.");
                return;
              }
            } else if (event == MEDIA_DRM_EVENT_KEY_EXPIRED) {
              Log.d(TAG, "MediaDrm.EVENT_KEY_EXPIRED");
            } else if (event == MediaDrm.EVENT_VENDOR_DEFINED) {
              Log.d(TAG, "MediaDrm.EVENT_VENDOR_DEFINED");
            } else if (event == MEDIA_DRM_EVENT_PROVISION_REQUIRED) {
              Log.d(TAG, "MediaDrm.EVENT_PROVISION_REQUIRED");
            } else if (event == MEDIA_DRM_EVENT_SESSION_RECLAIMED) {
              Log.d(TAG, "MediaDrm.EVENT_SESSION_RECLAIMED");
            } else {
              Log.e(TAG, "Invalid DRM event " + event);
              return;
            }
          }
        });

    mMediaDrm.setOnKeyStatusChangeListener(
        new MediaDrm.OnKeyStatusChangeListener() {
          @Override
          public void onKeyStatusChange(
              MediaDrm md,
              byte[] sessionId,
              List<MediaDrm.KeyStatus> keyInformation,
              boolean hasNewUsableKey) {
            MediaDrmBridgeJni.get().onKeyStatusChange(
                mNativeMediaDrmBridge,
                sessionId,
                keyInformation.stream()
                    .map(keyStatus -> new KeyStatus(keyStatus.getKeyId(), keyStatus.getStatusCode()))
                    .toArray(KeyStatus[]::new));
          }
        },
        null);

    mMediaDrm.setPropertyString("privacyMode", "disable");
    mMediaDrm.setPropertyString("sessionSharing", "enable");
    if (keySystem.equals("com.youtube.widevine.l3")
        && !mMediaDrm.getPropertyString("securityLevel").equals("L3")) {
      mMediaDrm.setPropertyString("securityLevel", "L3");
    }
  }

  private static String bytesToString(byte[] bytes) {
    if (bytes == null) {
      return "(null)";
    }
    try {
      return StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(bytes)).toString();
    } catch (Exception e) {
      return "hex(" + bytesToHexString(bytes) + ")";
    }
  }

  /** Convert byte array to hex string for logging. */
  private static String bytesToHexString(byte[] bytes) {
    StringBuilder hexString = new StringBuilder();
    for (int i = 0; i < bytes.length; ++i) {
      hexString.append(HEX_CHAR_LOOKUP[bytes[i] >>> 4]);
      hexString.append(HEX_CHAR_LOOKUP[bytes[i] & 0xf]);
    }
    return hexString.toString();
  }

  private void handleKeyRequiredEventWithAppProvisioning(byte[] sessionId, byte[] data) {
    assert mEnableAppProvisioning;
    if (sessionId == null) {
      Log.e(TAG, "HandleKeyRequiredEventWithAppProvisioning failed: null session id");
      return;
    }
    ByteBuffer sessionIdByteBuffer = ByteBuffer.wrap(sessionId);
    if (!mSessionIds.containsKey(sessionIdByteBuffer)) {
      Log.e(TAG, "HandleKeyRequiredEventWithAppProvisioning failed: invalid session id=" + bytesToString(sessionId));
      return;
    }

    String mime = mSessionIds.get(sessionIdByteBuffer);
    MediaDrm.KeyRequest request = null;
    try {
      request = getKeyRequest(sessionId, data, mime);
    } catch (Exception e) {
      Log.e(TAG, "getKeyRequest(sessionId=" + bytesToString(sessionId) + ") failed.", e);
      return;
    }
    if (request == null) {
      Log.e(TAG, "handleKeyRequiredEventWithAppProvisioning: getKeyRequest returned null");
      return;
    }

    onSessionMessage(SB_DRM_TICKET_INVALID, sessionId, request);
  }

  private void onSessionMessage(
      int ticket, final byte[] sessionId, final MediaDrm.KeyRequest request) {
    if (!isNativeMediaDrmBridgeValid()) {
      return;
    }

    int requestType = request.getRequestType();

    MediaDrmBridgeJni.get().onSessionMessage(
        mNativeMediaDrmBridge, ticket, sessionId, requestType, request.getData());
  }

  /**
   * Get a key request.
   *
   * @param sessionId ID of session on which we need to get the key request.
   * @param data Data needed to get the key request.
   * @param mime Mime type to get the key request.
   * @return the key request.
   */
  private MediaDrm.KeyRequest getKeyRequest(byte[] sessionId, byte[] data, String mime)
      throws android.media.NotProvisionedException {
    if (mMediaDrm == null) {
      throw new IllegalStateException("mMediaDrm cannot be null in getKeyRequest");
    }
    if (mMediaCryptoSession == null) {
      throw new IllegalStateException("mMediaCryptoSession cannot be null in getKeyRequest.");
    }
    // TODO: Cannot do this during provisioning pending.

    HashMap<String, String> optionalParameters = new HashMap<>();
    MediaDrm.KeyRequest request = null;
    try {
      request =
          mMediaDrm.getKeyRequest(
              sessionId, data, mime, MediaDrm.KEY_TYPE_STREAMING, optionalParameters);
    } catch (IllegalStateException e) {
      if (e instanceof android.media.MediaDrm.MediaDrmStateException) {
        Log.e(TAG, "MediaDrmStateException fired during getKeyRequest().", e);
      }
    }

    String result = (request != null) ? "succeeded" : "failed";
    Log.d(TAG, String.format("getKeyRequest %s!", result));

    return request;
  }

  /**
   * Create a MediaCrypto object.
   *
   * @return false upon fatal error in creating MediaCrypto. Returns true otherwise, including the
   *     following two cases: 1. MediaCrypto is successfully created and notified. 2. Device is not
   *     provisioned and MediaCrypto creation will be tried again after the provisioning process is
   *     completed.
   *     <p>When false is returned, the caller should call release(), which will notify the native
   *     code with a null MediaCrypto, if needed.
   */
  private boolean createMediaCrypto() {
    if (mMediaDrm == null) {
      throw new IllegalStateException("Cannot create media crypto with null mMediaDrm.");
    }
    // Create MediaCrypto object.
    try {
      if (MediaCrypto.isCryptoSchemeSupported(mSchemeUUID)) {
        MediaCrypto mediaCrypto = new MediaCrypto(mSchemeUUID, new byte[0]);
        Log.d(TAG, "MediaCrypto successfully created!");
        mMediaCrypto = mediaCrypto;
        return true;
      } else {
        Log.e(TAG, "Cannot create MediaCrypto for unsupported scheme.");
      }
    } catch (MediaCryptoException e) {
      Log.e(TAG, "Cannot create MediaCrypto", e);
    }

    return false;
  }

  /**
   * Open a new session.
   *
   * @return ID of the session opened. Returns null if unexpected error happened.
   */
  private byte[] openSession() throws android.media.NotProvisionedException {
    Log.d(TAG, "openSession()");
    if (mMediaDrm == null) {
      throw new IllegalStateException("mMediaDrm cannot be null in openSession");
    }
    try {
      byte[] sessionId = mMediaDrm.openSession();
      // Make a clone here in case the underlying byte[] is modified.
      return sessionId.clone();
    } catch (RuntimeException e) { // TODO: Drop this?
      Log.e(TAG, "Cannot open a new session", e);
      release();
      return null;
    } catch (NotProvisionedException e) {
      // Throw NotProvisionedException so that we can attemptProvisioning().
      throw e;
    } catch (MediaDrmException e) {
      // Other MediaDrmExceptions (e.g. ResourceBusyException) are not
      // recoverable.
      Log.e(TAG, "Cannot open a new session", e);
      release();
      return null;
    }
  }

  private void closeMediaDrmSession(byte[] sessionId) {
    if (sessionId == null) {
      Log.w(TAG, "Trying to close drm session with null sessionId. Ignored.");
      return;
    }

    try {
      // Some implementations let this method throw exceptions.
      mMediaDrm.closeSession(sessionId);
    } catch (Exception e) {
      Log.e(TAG, "closeSession(sessionId=" + bytesToString(sessionId) + ") failed: ", e);
    }
  }

  @CalledByNative
  boolean createMediaCryptoSession() {
    if (mMediaCryptoSession != null) {
      return true;
    }
    Log.w(TAG, "MediaDrmBridge createMediaCryptoSession");
    if (mMediaCrypto == null) {
      throw new IllegalStateException("Cannot create media crypto session with null mMediaCrypto.");
    }

    // Open media crypto session.
    try {
      mMediaCryptoSession = openSession();
    } catch (NotProvisionedException e) {
      Log.w(TAG, "Device not provisioned", e);
      if (!attemptProvisioning()) {
        Log.e(TAG, "Failed to provision device during MediaCrypto creation.");
        return false;
      }
      try {
        mMediaCryptoSession = openSession();
      } catch (NotProvisionedException e2) {
        Log.e(TAG, "Device still not provisioned after supposedly successful provisioning", e2);
        return false;
      }
    }

    if (mMediaCryptoSession == null) {
      Log.e(TAG, "Cannot create MediaCrypto Session.");
      return false;
    }

    try {
      mMediaCrypto.setMediaDrmSession(mMediaCryptoSession);
    } catch (MediaCryptoException e3) {
      Log.e(TAG, "Unable to set media drm session", e3);
      closeMediaDrmSession(mMediaCryptoSession);
      mMediaCryptoSession = null;
      return false;
    }

    Log.d(TAG, "MediaCrypto Session created: sessionId=" + bytesToString(mMediaCryptoSession));

    return true;
  }

  OperationResult createMediaCryptoSessionWithAppProvisioning() {
    assert mEnableAppProvisioning;
    if (mMediaCryptoSession != null) {
      Log.i(TAG, "MediaCryptoSession is already created");
      return OperationResult.success();
    }
    assert mMediaCrypto != null;

    byte[] mediaCryptoSession;
    try {
      mediaCryptoSession = openSession();
    } catch (NotProvisionedException e) {
      return OperationResult.notProvisioned(e);
    }
    if (mediaCryptoSession == null) {
      return OperationResult.operationFailed("openSession returned null");
    }

    try {
      mMediaCrypto.setMediaDrmSession(mediaCryptoSession);
    } catch (MediaCryptoException e) {
      closeMediaDrmSession(mediaCryptoSession);
      return OperationResult.operationFailed("Unable to set media drm session", e);
    }

    Log.i(TAG, "MediaCrypto Session created: sessionId=" + bytesToString(mediaCryptoSession));
    mMediaCryptoSession = mediaCryptoSession;
    return OperationResult.success();
  }

  @CalledByNative
  byte[] generateProvisionRequest() {
    assert mEnableAppProvisioning;
    MediaDrm.ProvisionRequest request = mMediaDrm.getProvisionRequest();
    Log.i(TAG, "start provisioning: request size=" + request.getData().length);

    return request.getData();
  }

  @CalledByNative
  OperationResult provideProvisionResponse(byte[] response) {
    assert mEnableAppProvisioning;

    Log.i(TAG, "handleProvisionResponse: size=" + response.length);

    try {
      mMediaDrm.provideProvisionResponse(response);
    } catch (android.media.DeniedByServerException e) {
      Log.e(TAG, "Failed to provide provision response.", e);
      return OperationResult.operationFailed("Failed to provide provision response.", e);
    }
    Log.i(TAG, "provideProvisionResponse succeeded");

    return OperationResult.success();
  }

  /**
   * Attempt to get the device that we are currently running on provisioned.
   *
   * @return whether provisioning was successful or not.
   */
  private boolean attemptProvisioning() {
    Log.d(TAG, "attemptProvisioning()");
    MediaDrm.ProvisionRequest request = mMediaDrm.getProvisionRequest();
    String url = request.getDefaultUrl() + "&signedRequest=" + new String(request.getData());
    byte[] response = new CobaltHttpHelper().performDrmHttpPost(url);
    if (response == null) {
      return false;
    }
    try {
      mMediaDrm.provideProvisionResponse(response);
      return true;
    } catch (android.media.DeniedByServerException e) {
      Log.e(TAG, "failed to provide provision response", e);
    } catch (java.lang.IllegalStateException e) {
      Log.e(TAG, "failed to provide provision response", e);
    }
    return false;
  }

  /**
   * Check whether |sessionId| is an existing session ID, excluding the media crypto session.
   *
   * @param sessionId Crypto session Id.
   * @return true if |sessionId| exists, false otherwise.
   */
  private boolean sessionExists(byte[] sessionId) {
    if (mMediaCryptoSession == null) {
      if (!mSessionIds.isEmpty()) {
        throw new IllegalStateException(
            "mSessionIds must be empty if crypto session does not exist.");
      }
      Log.e(TAG, "Session doesn't exist because media crypto session is not created.");
      return false;
    }
    return !Arrays.equals(sessionId, mMediaCryptoSession)
        && mSessionIds.containsKey(ByteBuffer.wrap(sessionId));
  }

  /** Release all allocated resources and finish all pending operations. */
  private void release() {
    // Note that mNativeMediaDrmBridge may have already been reset (see destroy()).
    if (mMediaDrm == null) {
      throw new IllegalStateException("Called release with null mMediaDrm.");
    }

    // Close all open sessions.
    for (ByteBuffer sessionIdByteBuffer : mSessionIds.keySet()) {
      byte[] sessionId = sessionIdByteBuffer.array();
      try {
        // Some implementations don't have removeKeys.
        // https://bugs.chromium.org/p/chromium/issues/detail?id=475632
        mMediaDrm.removeKeys(sessionId);
      } catch (Exception e) {
        Log.e(TAG, "removeKeys failed: ", e);
      }

      closeMediaDrmSession(sessionId);
      Log.d(TAG, "Successfully closed session: sessionId=", bytesToString(sessionId));
    }
    mSessionIds.clear();

    // Close mMediaCryptoSession if it's open.
    if (mMediaCryptoSession != null) {
      closeMediaDrmSession(mMediaCryptoSession);
      mMediaCryptoSession = null;
    }

    if (mMediaDrm != null) {
      if (Build.VERSION.SDK_INT >= 28) {
        closeMediaDrmV28(mMediaDrm);
      } else {
        releaseMediaDrmDeprecated(mMediaDrm);
      }
      mMediaDrm = null;
    }
  }

  @SuppressWarnings("deprecation")
  private void releaseMediaDrmDeprecated(MediaDrm mediaDrm) {
    mediaDrm.release();
  }

  @RequiresApi(28)
  private void closeMediaDrmV28(MediaDrm mediaDrm) {
    mediaDrm.close();
  }

  @RequiresApi(29)
  private void getConnectedHdcpLevelInfoV29(MediaDrm mediaDrm) {
    int hdcpLevel = mediaDrm.getConnectedHdcpLevel();
    switch (hdcpLevel) {
      case MediaDrm.HDCP_V1:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_V1.");
        break;
      case MediaDrm.HDCP_V2:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_V2.");
        break;
      case MediaDrm.HDCP_V2_1:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_V2_1.");
        break;
      case MediaDrm.HDCP_V2_2:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_V2_2.");
        break;
      case MediaDrm.HDCP_V2_3:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_V2_3.");
        break;
      case MediaDrm.HDCP_NONE:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_NONE.");
        break;
      case MediaDrm.HDCP_NO_DIGITAL_OUTPUT:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_NO_DIGITAL_OUTPUT.");
        break;
      case MediaDrm.HDCP_LEVEL_UNKNOWN:
        Log.i(TAG, "MediaDrm HDCP Level is HDCP_LEVEL_UNKNOWN.");
        break;
      default:
        Log.i(TAG, String.format(Locale.US, "Unknown MediaDrm HDCP level %d.", hdcpLevel));
        break;
    }
  }

  private boolean isNativeMediaDrmBridgeValid() {
    return mNativeMediaDrmBridge != INVALID_NATIVE_MEDIA_DRM_BRIDGE;
  }

  /** A wrapper of the android MediaDrm.KeyStatus class to be used by JNI. */
  public static class KeyStatus {
    private final byte[] mKeyId;
    private final int mStatusCode;

    private KeyStatus(byte[] keyId, int statusCode) {
      mKeyId = (keyId == null) ? null : keyId.clone();
      mStatusCode = statusCode;
    }

    @CalledByNative("KeyStatus")
    private byte[] getKeyId() {
      return (mKeyId == null) ? null : mKeyId.clone();
    }

    @CalledByNative("KeyStatus")
    private int getStatusCode() {
      return mStatusCode;
    }
  }

  @NativeMethods
  interface Natives {
    void onSessionMessage(
        long nativeMediaDrmBridge,
        int ticket,
        byte[] sessionId,
        int requestType,
        byte[] message);

    void onKeyStatusChange(
        long nativeMediaDrmBridge,
        byte[] sessionId,
        @JniType("std::vector<DrmKeyStatusInfo>") KeyStatus[] keyInformation);
  }
}
