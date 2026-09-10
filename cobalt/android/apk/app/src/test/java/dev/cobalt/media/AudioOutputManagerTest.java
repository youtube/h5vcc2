/*
 * Copyright 2026 The Cobalt Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package dev.cobalt.media;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.content.Context;
import android.media.AudioDeviceCallback;
import android.media.AudioManager;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.util.ReflectionHelpers;

@RunWith(RobolectricTestRunner.class)
public class AudioOutputManagerTest {
  @Test
  public void testAudioDeviceListenerRemainsRegisteredUntilLastRegistrantIsRemoved() {
    Context context = mock(Context.class);
    Context applicationContext = mock(Context.class);
    AudioManager audioManager = mock(AudioManager.class);
    AudioDeviceCallback callback =
        ReflectionHelpers.getStaticField(AudioOutputManager.class, "sAudioDeviceCallback");
    AtomicInteger listenerRefCount =
        ReflectionHelpers.getStaticField(AudioOutputManager.class, "sAudioDeviceListenerRefCount");
    listenerRefCount.set(0);
    when(context.getApplicationContext()).thenReturn(applicationContext);
    when(applicationContext.getSystemService(Context.AUDIO_SERVICE)).thenReturn(audioManager);

    AudioOutputManager.addAudioDeviceListener(context);
    AudioOutputManager.addAudioDeviceListener(context);
    AudioOutputManager.removeAudioDeviceListener(context);

    verify(audioManager, times(1)).registerAudioDeviceCallback(callback, null);
    verify(audioManager, never()).unregisterAudioDeviceCallback(callback);

    AudioOutputManager.removeAudioDeviceListener(context);

    verify(audioManager, times(1)).unregisterAudioDeviceCallback(callback);
  }

  @Test
  public void testMissingAudioManagerDoesNotChangeListenerRefCount() {
    Context context = mock(Context.class);
    Context applicationContext = mock(Context.class);
    AtomicInteger listenerRefCount =
        ReflectionHelpers.getStaticField(AudioOutputManager.class, "sAudioDeviceListenerRefCount");
    listenerRefCount.set(0);
    when(context.getApplicationContext()).thenReturn(applicationContext);
    when(applicationContext.getSystemService(Context.AUDIO_SERVICE)).thenReturn(null);

    AudioOutputManager.addAudioDeviceListener(context);
    AudioOutputManager.removeAudioDeviceListener(context);

    assertEquals(0, listenerRefCount.get());
  }

  @Test
  public void testExtraRemovalDoesNotPoisonListenerRefCount() {
    Context context = mock(Context.class);
    Context applicationContext = mock(Context.class);
    AudioManager audioManager = mock(AudioManager.class);
    AudioDeviceCallback callback =
        ReflectionHelpers.getStaticField(AudioOutputManager.class, "sAudioDeviceCallback");
    AtomicInteger listenerRefCount =
        ReflectionHelpers.getStaticField(AudioOutputManager.class, "sAudioDeviceListenerRefCount");
    listenerRefCount.set(0);
    when(context.getApplicationContext()).thenReturn(applicationContext);
    when(applicationContext.getSystemService(Context.AUDIO_SERVICE)).thenReturn(audioManager);

    AudioOutputManager.removeAudioDeviceListener(context);

    assertEquals(0, listenerRefCount.get());
    verify(audioManager, never()).unregisterAudioDeviceCallback(callback);

    AudioOutputManager.addAudioDeviceListener(context);

    verify(audioManager, times(1)).registerAudioDeviceCallback(callback, null);
  }

  @Test
  public void testNullContextDoesNotChangeListenerRefCount() {
    AtomicInteger listenerRefCount =
        ReflectionHelpers.getStaticField(AudioOutputManager.class, "sAudioDeviceListenerRefCount");
    listenerRefCount.set(0);

    AudioOutputManager.addAudioDeviceListener(null);
    AudioOutputManager.removeAudioDeviceListener(null);

    assertEquals(0, listenerRefCount.get());
  }
}
