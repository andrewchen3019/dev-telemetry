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
import { Data } from '../../components/Data'
import { useMessageDataSource } from '@electricui/core-timeseries'
import React from 'react'
import { RouteComponentProps } from '@reach/router'
import { Slider } from '@electricui/components-desktop-blueprint'

import { Button} from '@electricui/components-desktop-blueprint'
import { ButtonGroup } from '@blueprintjs/core'

const layoutDescription = `
  ChartSpeed ChartBattery ChartLED
  Light      Slider       Slider
  Button
`
const deviceManager = useDeviceManager() as any     // cast to 'any' to reach internals
  const device =
    deviceManager.connectedDevices?.[0] ??
    deviceManager.devices?.[0] ??
    null

  const propulsionOn = () => device?.write({ propulsion: 1 })
  const propulsionOff = () => device?.write({ propulsion: 0 })

export const OverviewPage = (props: RouteComponentProps) => {
  const ledStateDataSource = useMessageDataSource('led_state');
  const batteryDataSource = useMessageDataSource('battery');
  const speedDataSource = useMessageDataSource('speed');
  
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
                  <LineChart key="battery" dataSource={batteryDataSource} />
                  <RealTimeDomain window={10000} />
                  <TimeAxis />
                  <VerticalAxis />
                </ChartContainer>
              </Card>
            </Areas.ChartBattery>

           {/* DISPLAYS LED STATE */}
            <Areas.ChartLED>
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
            </Areas.ChartLED>

            <Areas.Light>
              <LightBulb containerStyle={{ margin: '20px auto', width: '80%' }} width="40vw" />
            </Areas.Light>

          {/* SLIDER FOR LED FREQUENCY */}
            <Areas.Slider>
              <Card>
                <div style={{ margin: 20 }}>
                  <Slider
                    min={20}
                    max={1020}
                    stepSize={10}
                    labelStepSize={100}
                    sendOnlyOnRelease
                  >
                    <Slider.Handle accessor="lit_time" />
                  </Slider>
                </div>
              </Card>
            </Areas.Slider>

            {/* BUTTON TO CONTROL PROPULSION SWITCH */}
            {/* NOTE: PERHAPS SWITCH TO SLIDER FOR BETTER CONTROL? */}
            <Areas.Button>
              <Card>
              <div >
                <ButtonGroup>
                  <Button intent="success" large onClick={propulsionOn}>
                      Propulsion On
                  </Button>
                  
                  <Button intent="danger"  large onClick = {propulsionOff}>
                      Propulsion Off 
                  </Button>

                </ButtonGroup>

              </div>
              </Card>
            </Areas.Button>

          </React.Fragment>
        )}
      </Composition>
    </React.Fragment>
  )
}
