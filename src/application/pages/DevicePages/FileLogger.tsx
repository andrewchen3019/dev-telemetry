import { PolledCSVLogger } from '@electricui/components-desktop-blueprint-timeseries'
import React from 'react'
import { RouteComponentProps } from '@reach/router'
import { useMessageDataSource } from '@electricui/core-timeseries'

export const FileLogger = (props: RouteComponentProps) => {
  const motorRpmSource       = useMessageDataSource('motor_rpm_lo')
  const throttleSource       = useMessageDataSource('throttle_pct')
  const joulemeterLoSource   = useMessageDataSource('joulemeter_lo')
  const joulemeterCurrentSrc = useMessageDataSource('joulemeter_current')
  const joulemeterVoltageSrc = useMessageDataSource('joulemeter_voltage')
  const propulsionSource     = useMessageDataSource('propulsion_state')

  return (
    <React.Fragment>
      <PolledCSVLogger
        interval={10}
        columns={[
          { dataSource: motorRpmSource,       column: 'RPM (x0.001)' },
          { dataSource: throttleSource,       column: 'Throttle (x0.1 %)' },
          { dataSource: joulemeterLoSource,   column: 'Energy (x0.001 J)' },
          { dataSource: joulemeterCurrentSrc, column: 'Current (x0.001 A)' },
          { dataSource: joulemeterVoltageSrc, column: 'Voltage (x0.001 V)' },
          { dataSource: propulsionSource,     column: 'Propulsion' },
        ]}
      />
    </React.Fragment>
  )
}