import { RouteComponentProps, Router } from '@reach/router'

import { Intent } from '@blueprintjs/core'
import { IconNames } from '@blueprintjs/icons'
import { DisconnectionModal } from '@electricui/components-desktop-blueprint'
import { navigate } from '@electricui/utility-electron'
import React from 'react'
import { Header } from '../../components/Header'
import { OverviewPage } from './OverviewPage'
import { SecondaryPage } from './SecondaryPage'

interface InjectDeviceIDFromLocation {
  deviceID?: string
}

export const DevicePages = (
  props: RouteComponentProps & InjectDeviceIDFromLocation,
) => {
  if (!props.deviceID) {
    return <div>No deviceID?</div>
  }

  return (
    <React.Fragment>
      <DisconnectionModal
        intent={Intent.WARNING}
        icon={IconNames.SATELLITE}
        navigateToConnectionsScreen={() => navigate('/')}
      >
        <p>
          Connection has been lost with your device. If we successfully
          reconnect this dialog will be dismissed.
        </p>
      </DisconnectionModal>

      <div className="device-pages">
        <Header deviceID={props.deviceID} {...props} />
        <div className="device-content">
          <Router primary={false}>
            <OverviewPage path="/" />
            <SecondaryPage path="secondary" />
          </Router>
        </div>
      </div>
    </React.Fragment>
  )
}
