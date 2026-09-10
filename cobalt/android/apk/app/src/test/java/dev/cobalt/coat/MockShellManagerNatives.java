/*
 * Copyright 2022 The Cobalt Authors. All Rights Reserved.
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
package dev.cobalt.coat;

import dev.cobalt.shell.ShellManager;

/** Mock ShellManager.Natives implementation. */
public class MockShellManagerNatives implements ShellManager.Natives {
  public void onShellCreated(ShellManager caller, long nativeShell) {}

  @Override
  public void launchShell(Object shellManagerInstance, String url, String deepLinkUrl) {}

  @Override
  public void init(Object shellManagerInstance) {}

  @Override
  public void destroy(Object shellManagerInstance) {}

  @Override
  public String appendMigrationStatus(String url) {
    return url;
  }
}
