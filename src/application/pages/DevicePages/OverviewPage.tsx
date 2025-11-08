import {
  ChartContainer,
  LineChart,
  RealTimeDomain,
  TimeAxis,
  VerticalAxis,
} from '@electricui/components-desktop-charts'

import { Card } from '@blueprintjs/core'
import { Composition } from 'atomic-layout'
import { IntervalRequester } from '@electricui/components-core'
import { LightBulb } from '../../components/LightBulb'
// import { Data } from '../../components/Data'
import { useMessageDataSource } from '@electricui/core-timeseries'
import React from 'react'
import { RouteComponentProps } from '@reach/router'
import { Slider } from '@electricui/components-desktop-blueprint'

const layoutDescription = `
  ChartSpeed ChartBattery ChartLED
  Light      Slider       Slider
`

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
          </React.Fragment>
        )}
      </Composition>
    </React.Fragment>
  )
}
