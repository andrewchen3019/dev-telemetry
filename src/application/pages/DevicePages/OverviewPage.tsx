import {
  ChartContainer,
  LineChart,
  RealTimeDomain,
  TimeAxis,
  VerticalAxis,
} from '@electricui/components-desktop-charts'

import { Card } from '@blueprintjs/core'
import { Composition } from 'atomic-layout'
import { IntervalRequester, useDeviceManager } from '@electricui/components-core'
import { LightBulb } from '../../components/LightBulb'
import { useMessageDataSource } from '@electricui/core-timeseries'
import React, { useState } from 'react'
import { RouteComponentProps } from '@reach/router'
import { Slider } from '@electricui/components-desktop-blueprint'
import { Switch as BPSwitch } from '@blueprintjs/core';
import { Statistic } from '@electricui/components-desktop-blueprint'

const layoutDescription = `
  ChartSpeed ChartBattery
  Slider Switch Statistic
`


export const OverviewPage = (props: RouteComponentProps) => {
  const ledStateDataSource = useMessageDataSource('led_state');
  const batteryEfficiencyDataSource = useMessageDataSource('battery'); //vehicle efficiency 
  const speedDataSource = useMessageDataSource('speed'); //vehicle speed

  const deviceManager = useDeviceManager() as any     // cast to 'any' to reach internals
  const device =
    deviceManager.connectedDevices?.[0] ??
    deviceManager.devices?.[0] ??
    null

  // local, controlled state for the propulsion switch (optimistic UI)
  const [propulsionOnState, setPropulsionOnState] = useState<boolean>(false)


  const handlePropulsionToggle = (e: any) => {
    // Switch from electricui/blueprint can call onChange with an event or boolean.
    const checked =
      typeof e === 'boolean' ? e : (e && e.target ? !!e.target.checked : !propulsionOnState)

    // optimistic UI update
    setPropulsionOnState(checked)

    // send the command to the device
    // send 1 for on, 0 for off (matching your earlier device.write usage)
    if (device?.write) {
      device.write({ propulsion: checked ? 1 : 0 })
    } else {
      // dev fallback: log if no device is found
      // (remove this in production)
      // console.warn('No device available to write propulsion state', checked)
    }
  }

  return (
    <React.Fragment>
      <IntervalRequester interval={50} messageIDs={['led_state','battery','speed']} />

      <Composition areas={layoutDescription} gap={10} autoCols="1fr">
        {Areas => (
          <React.Fragment>

             {/* DISPLAYS SPEED */}
            <Areas.ChartSpeed>
              <Card>
                <div style={{ textAlign: 'center', marginBottom: '1em' }}>
                  <b>Speed</b>
                </div>
                <ChartContainer>
                  <LineChart key="speed" dataSource={speedDataSource} />
                  <RealTimeDomain window={10000} />
                  <TimeAxis />
                  <VerticalAxis />
                </ChartContainer>
              </Card>
            </Areas.ChartSpeed>

            {/* DISPLAYS BATTERY EFFICIENCY */}
            <Areas.ChartBattery>
              <Card>
                <div style={{ textAlign: 'center', marginBottom: '1em' }}>
                  <b>Battery Efficiency</b>
                </div>
                <ChartContainer>
                  <LineChart key="battery" dataSource={batteryEfficiencyDataSource} />
                  <RealTimeDomain window={10000} />
                  <TimeAxis />
                  <VerticalAxis />
                </ChartContainer>
              </Card>
            </Areas.ChartBattery>

           {/* DISPLAYS LED STATE */}
            {/* <Areas.ChartLED>
              <Card>
                <div style={{ textAlign: 'center', marginBottom: '1em' }}>
                  <b>LED State</b>
                </div>
                <ChartContainer>
                  <LineChart key="led" dataSource={ledStateDataSource} />
                  <RealTimeDomain window={10000} />
                  <TimeAxis />
                  <VerticalAxis />
                </ChartContainer>
              </Card>
            </Areas.ChartLED> */}

            {/* <Areas.Light>
              <LightBulb containerStyle={{ margin: '20px auto', width: '80%' }} width="40vw" />
            </Areas.Light> */}

          {/* SLIDER FOR LED FREQUENCY */}
            <Areas.Slider>

              <Card>
                <div style={{ margin: 20 }}>
                    <div style={{ margin: 10 }}>Transmission Frequency (ms) </div>
                  <Slider
                    min={20}
                    max={120}
                    stepSize={5}
                    labelStepSize={5}
                    sendOnlyOnRelease
                  >
                    <Slider.Handle accessor="lit_time" />
                  </Slider>
                </div>
              </Card>
            </Areas.Slider>

            {/* PROPULSION SWITCH */}
            <Areas.Switch>
              <Card>
                <div style={{ padding: 16, display: 'flex', alignItems: 'center', gap: 12 }}>
                 <BPSwitch
                    checked={propulsionOnState}
                    onChange={handlePropulsionToggle}
                    >
                    Toggle Propulsion
                  </BPSwitch>
                </div>
              </Card>
            </Areas.Switch>

            {/* Voltage Reading */}
            <Areas.Statistic>
              <Card>
                <div style={{ padding: 16, display: 'flex', alignItems: 'center', gap: 12 }}>
                  <Statistic accessor="boiler_w" 
                    label="Propulsion Voltage"
                    suffix = "V"
                    color="#e72305ff" />
                </div>
              </Card>
            </Areas.Statistic>

          </React.Fragment>
        )}
      </Composition>
    </React.Fragment>
  )
}