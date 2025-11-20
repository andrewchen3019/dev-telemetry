import { PolledCSVLogger } from '@electricui/components-desktop-blueprint-timeseries';
import React from 'react'
import { RouteComponentProps } from '@reach/router'
import { Card } from '@blueprintjs/core'
import { useMessageDataSource } from '@electricui/core-timeseries'


export const FileLogger = (props: RouteComponentProps) => {
 const batteryEfficiencyDataSource = useMessageDataSource('battery'); //vehicle efficiency 
  const speedDataSource = useMessageDataSource('speed'); //vehicle speed
  return (
    <React.Fragment>
      <PolledCSVLogger
        interval={10}
        columns={[
          { dataSource: batteryEfficiencyDataSource, column: 'Battery Efficiency' },
         { dataSource: speedDataSource, column: 'Speed' },
        ]}
      />
    </React.Fragment>
  )
}
