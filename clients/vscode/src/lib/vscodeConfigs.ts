/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See License.txt in the project root for license information.
 *  Selected types checked in from https://github.com/Microsoft/vscode/blob/main/src/vs/platform/configuration/common/configurationRegistry.ts
 *--------------------------------------------------------------------------------------------*/

import { ConfigurationScope } from 'vscode'
import { IJSONSchema } from './jsonSchema'

export enum EditPresentationTypes {
  Multiline = 'multilineText',
  Singleline = 'singlelineText',
}
export type PolicyName = string

export interface IPolicy {
  /**
   * The policy name.
   */
  readonly name: PolicyName

  /**
   * The Code version in which this policy was introduced.
   */
  readonly minimumVersion: `${number}.${number}`
}

export interface IConfigurationPropertySchema extends IJSONSchema {
  scope?: ConfigurationScope

  /**
   * When restricted, value of this configuration will be read only from trusted sources.
   * For eg., If the workspace is not trusted, then the value of this configuration is not read from workspace settings file.
   */
  restricted?: boolean

  /**
   * When `false` this property is excluded from the registry. Default is to include.
   */
  included?: boolean

  /**
   * List of tags associated to the property.
   *  - A tag can be used for filtering
   *  - Use `experimental` tag for marking the setting as experimental. **Note:** Defaults of experimental settings can be changed by the running experiments.
   */
  tags?: string[]

  /**
   * When enabled this setting is ignored during sync and user can override this.
   */
  ignoreSync?: boolean

  /**
   * When enabled this setting is ignored during sync and user cannot override this.
   */
  disallowSyncIgnore?: boolean

  /**
   * Labels for enumeration items
   */
  enumItemLabels?: string[]

  /**
   * When specified, controls the presentation format of string settings.
   * Otherwise, the presentation format defaults to `singleline`.
   */
  editPresentation?: EditPresentationTypes

  /**
   * When specified, gives an order number for the setting
   * within the settings editor. Otherwise, the setting is placed at the end.
   */
  order?: number

  /**
   * When specified, this setting's value can always be overwritten by
   * a system-wide policy.
   */
  policy?: IPolicy
}
