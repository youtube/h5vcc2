#!/usr/bin/python
#
# Copyright 2017 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Adds common command-line arguments to a provided argument parser."""

import argparse
from starboard.build.platforms import PLATFORMS
from starboard.tools import params
import starboard.tools.config


def AddLoggingArguments(arg_parser, default='info'):
  """Adds the logging level configuration argument.

  Args:
    arg_parser: The argument parser to used for initialization.
    default: The default logging level to use. Valid values are: 'info',
      'debug', 'warning', 'error', and 'critical'.
  """
  arg_parser.add_argument(
      '--log_level',
      choices=['info', 'debug', 'warning', 'error', 'critical'],
      default=default,
      help='The minimum level a log statement must be to be output. This value '
      "is used to initialize the 'logging' module log level.")


def AddPlatformConfigArguments(arg_parser):
  """Adds the platform configuration arguments required for building."""
  AddLoggingArguments(arg_parser)
  arg_parser.add_argument(
      '-p',
      '--platform',
      choices=list(PLATFORMS.keys()),
      required=True,
      help="Device platform, eg 'linux-x64x11'. Requires that you have "
      'already run gn.py for the desired platform.')
  arg_parser.add_argument(
      '-c',
      '--config',
      choices=starboard.tools.config.GetAll(),
      required=True,
      help="Build config (eg, 'qa' or 'devel')")
  arg_parser.add_argument(
      '-P',
      '--loader_platform',
      default=None,
      help='Specifies the platform to build the loader with. This flag is only '
      'relevant for Evergreen builds, and should be the platform you intend '
      "to run your tests on (eg 'linux-x64x11', or 'rdk-arm'). "
      'Requires that --loader_config be given, and that you have already '
      'run gn.py for the desired loader platform.')
  arg_parser.add_argument(
      '-C',
      '--loader_config',
      default=None,
      help="Specifies the config to build the loader with (eg 'qa' or 'devel')."
      'This flag is only relevant for Evergreen builds, and requires that '
      '--loader_platform be given.')


def AddLauncherArguments(arg_parser):
  """
  Adds the platform configuration and device information arguments require by
  launchers.
  """
  AddPlatformConfigArguments(arg_parser)
  arg_parser.add_argument(
      '-d', '--device_id', help='Devkit or IP address for the target device.')
  arg_parser.add_argument(
      '--target_params',
      help='Command line arguments to pass to the executable.'
      ' Because different executables could have differing command'
      ' line syntax, list all arguments exactly as you would to the'
      ' executable between a set of double quotation marks.')
  arg_parser.add_argument(
      '-o',
      '--out_directory',
      help='Directory containing tool binaries or their components.'
      ' Automatically derived if absent.')
  arg_parser.add_argument(
      '-O',
      '--loader_out_directory',
      default=None,
      help='Identical to --out_directory, except used for the loader platform '
      'when building and running in Evergreen mode.')


def CreatePlatformConfigParams(arg_parser=None):
  """Returns a PlatformConfigParams object initialized from an argument parser.

  This is a helper function that simplifies the creation of a
  PlatformConfigParams object. This function will create an argument parser if
  one is not provided, however if one is provided it is assumed that it has the
  necessary arguments added.

  Args:
    arg_parser: The argument parser to used for initialization. When |None| it
      will be created and initialized with the minimum required parameters.
  """
  if not arg_parser:
    arg_parser = argparse.ArgumentParser()
    AddPlatformConfigArguments(arg_parser)
  platform_config_params = params.PlatformConfigParams()
  _, _ = arg_parser.parse_known_args(namespace=platform_config_params)
  return platform_config_params


def CreateLauncherParams(arg_parser=None):
  """Returns a LauncherParams object initialized from an argument parser.

  This is a helper function that simplifies the creation of a LauncherParams
  object. This function will create an argument parser if one is not provided,
  however if one is provided it is assumed that it has the necessary arguments
  added.

  Args:
    arg_parser: The argument parser to used for initialization. When |None| it
      will be created and initialized with the minimum required parameters.
  """
  if not arg_parser:
    arg_parser = argparse.ArgumentParser()
    AddLauncherArguments(arg_parser)
  launcher_params = params.LauncherParams()
  args, _ = arg_parser.parse_known_args(namespace=launcher_params)
  if args.target_params is None:
    launcher_params.target_params = []
  else:
    launcher_params.target_params = args.target_params.split(' ')
  return launcher_params
