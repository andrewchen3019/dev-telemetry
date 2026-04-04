import { PolledCSVLogger } from '@electricui/components-desktop-blueprint-timeseries';
import React from 'react'
import { RouteComponentProps } from '@reach/router'
import { Card } from '@blueprintjs/core'
import { useMessageDataSource } from '@electricui/core-timeseries'


export const FileLogger = (props: RouteComponentProps) => {
  const batteryEfficiencyDataSource = useMessageDataSource('battery')
  const speedDataSource = useMessageDataSource('speed')
  const distanceDataSource = useMessageDataSource('distance')
  const rssiDataSource = useMessageDataSource('rssi')
  const propulsionStateDataSource = useMessageDataSource('propulsion_state')
  return (
    <React.Fragment>
      <PolledCSVLogger
        interval={10}
        columns={[
          { dataSource: batteryEfficiencyDataSource, column: 'Battery Efficiency' },
         { dataSource: speedDataSource, column: 'Speed' },
         { dataSource: distanceDataSource, column: 'Distance' },
            { dataSource:rssiDataSource, column: 'Rssi' },
         { dataSource:propulsionStateDataSource, column: 'Propulsion' },
        ]}
      />
    </React.Fragment>
  )
}
