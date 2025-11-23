import {
  ChartContainer,
  LineChart,
  RealTimeDomain,
  TimeAxis,
  VerticalAxis,
} from '@electricui/components-desktop-charts'

import { Card, Button } from '@blueprintjs/core'
import { Composition } from 'atomic-layout'
import { IntervalRequester, useDeviceManager } from '@electricui/components-core'
import { useMessageDataSource } from '@electricui/core-timeseries'
import React, { useState, useEffect } from 'react'
import { RouteComponentProps } from '@reach/router'
import { Slider } from '@electricui/components-desktop-blueprint'
import { Statistic } from '@electricui/components-desktop-blueprint'

const layoutDescription = `
  ChartSpeed ChartBattery ChartDistance
  Slider Switch DistanceStat
  Statistic`


export const OverviewPage = (props: RouteComponentProps) => {
  const ledStateDataSource = useMessageDataSource('led_state')
  const batteryEfficiencyDataSource = useMessageDataSource('battery')
  const speedDataSource = useMessageDataSource('speed')
  const distanceDataSource = useMessageDataSource('dist') // <-- change if your message ID differs

  const deviceManager = useDeviceManager() as any
  const device =
    deviceManager.connectedDevices?.[0] ??
    deviceManager.devices?.[0] ??
    null

  // local, optimistic UI state for whether relay/propulsion is on
  const [propulsionOnState, setPropulsionOnState] = useState<boolean>(false)

  // Optional: sync state from the device if the device publishes a 'propulsion' or 'relay_state' message.
  // If your device publishes a message like 'propulsion' or 'relay_state', subscribe to it and keep local state in sync.
  // Example (uncomment if the message exists):
  // const propulsionStateSource = useMessageDataSource('propulsion_state')
  // useEffect(() => {
  //   const sub = propulsionStateSource.subscribe(v => {
  //     if (typeof v === 'number') setPropulsionOnState(Boolean(v))
  //   })
  //   return () => sub.unsubscribe()
  // }, [propulsionStateSource])

  const handleToggleRelay = async () => {
    const next = !propulsionOnState
    setPropulsionOnState(next) // optimistic UI

    // IMPORTANT: choose the message id that your bridge/connected device expects.
    // Many setups use e.g. { propulsion: 1 } (as in your example) — adapt below to match the bridge.
    if (device?.write) {
      // two example payload shapes — pick one that your bridge maps to a CMD_RELAY:
      // 1) { propulsion: 1 }       <-- if bridge listens to "propulsion"
      // 2) { relay: 1 }            <-- if bridge listens to "relay"
      // 3) { cmd_relay: 1 }        <-- or any other mapping implemented on the bridge
      //
      // CHANGE THIS OBJECT to match your bridge firmware's expected message key
      try {
        await device.write({ propulsion: next ? 1 : 0 })
      } catch (err) {
        // if write fails, revert optimistic update
        console.warn('device.write failed', err)
        setPropulsionOnState(!next)
      }
    } else {
      // dev fallback: no device connected
      console.warn('No device available to write propulsion state', next)
    }
  }

  // Distance numeric for Statistic card (last value)
  const [lastDistance, setLastDistance] = useState<number | null>(null)
  useEffect(() => {
    // subscribe to the distance data source (if available)
    // dataSource exposes `.subscribe` in the electricui timeseries implementation
    if (!distanceDataSource) return
    const sub = distanceDataSource.subscribe((v: any) => {
      // Expect a numeric payload in mm, adjust if your bridge publishes different units
      if (typeof v === 'number') setLastDistance(v)
      else if (v && typeof v.value === 'number') setLastDistance(v.value)
    })
    return () => sub.unsubscribe()
  }, [distanceDataSource])

  return (
    <React.Fragment>
      <IntervalRequester interval={50} messageIDs={['led_state','battery','speed','ultrasonic']} />

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
                  <LineChart key="battery" dataSource={batteryEfficiencyDataSource} />
                  <RealTimeDomain window={10000} />
                  <TimeAxis />
                  <VerticalAxis />
                </ChartContainer>
              </Card>
            </Areas.ChartBattery>

            <Areas.ChartDistance>
              <Card>
                <div style={{ textAlign: 'center', marginBottom: '1em' }}>
                  <b>Ultrasonic Distance (mm)</b>
                </div>
                <ChartContainer>
                  <LineChart key="distance" dataSource={distanceDataSource} />
                  <RealTimeDomain window={15000} />
                  <TimeAxis />
                  <VerticalAxis />
                </ChartContainer>
              </Card>
            </Areas.ChartDistance>

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

            <Areas.Switch>
              <Card>
                <div style={{ padding: 16, display: 'flex', alignItems: 'center', gap: 12 }}>
                  <Button
                    intent={propulsionOnState ? 'success' : 'primary'}
                    text={propulsionOnState ? 'TURN RELAY OFF' : 'TURN RELAY ON'}
                    onClick={handleToggleRelay}
                    large
                  />
                </div>
              </Card>
            </Areas.Switch>

            <Areas.DistanceStat>
              <Card>
                <div style={{ padding: 16, display: 'flex', alignItems: 'center', gap: 12, justifyContent: 'space-between' }}>
                  <div>
                    <div style={{ fontSize: 12, color: '#666' }}>Last Distance</div>
                    <div style={{ fontSize: 20 }}>
                      {lastDistance === null ? '—' : `${lastDistance} mm`}
                    </div>
                  </div>
                </div>
              </Card>
            </Areas.DistanceStat>

            <Areas.Statistic>
              <Card>
                <div style={{ padding: 16, display: 'flex', alignItems: 'center', gap: 12 }}>
                  <Statistic accessor="voltage"
                    label="Propulsion Voltage"
                    suffix = "V"
                    color="#ffab9eff" />
                </div>
              </Card>
            </Areas.Statistic>
          </React.Fragment>
        )}
      </Composition>
    </React.Fragment>
  )
}
