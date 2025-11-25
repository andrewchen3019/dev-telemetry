// OverviewPage.tsx
import React, { useState, useEffect, useRef, useMemo } from 'react'
import { RouteComponentProps } from '@reach/router'

import {
  ChartContainer,
  LineChart,
  RealTimeDomain,
  TimeAxis,
  VerticalAxis,
} from '@electricui/components-desktop-charts'

import { Card, Button } from '@blueprintjs/core'
import { IntervalRequester, useDeviceManager } from '@electricui/components-core'
import { useMessageDataSource } from '@electricui/core-timeseries'
import { Slider } from '@electricui/components-desktop-blueprint'
import { Statistic } from '@electricui/components-desktop-blueprint'

/**
 * A simple CSS-grid layout to avoid atomic-layout version/layout quirks.
 * Three columns on wide screens, stacks on narrow screens.
 */
const gridStyle: React.CSSProperties = {
  display: 'grid',
  gridTemplateColumns: 'repeat(3, 1fr)',
  gap: '12px',
  alignItems: 'stretch',
  width: '100%',
  boxSizing: 'border-box',
  padding: '12px',
  gridAutoRows: 'minmax(120px, auto)',
}

/** Helper to try several common accessors to return the latest numeric value. */
function getLatestFromDataSource(dataSource: any) {
  if (!dataSource) return undefined
  if (typeof dataSource.getLatest === 'function') {
    try { return dataSource.getLatest() } catch {}
  }
  if (dataSource.latest !== undefined) return dataSource.latest
  if (dataSource.latestValue !== undefined) return dataSource.latestValue
  if (dataSource.value !== undefined) return dataSource.value
  if (Array.isArray(dataSource.data) && dataSource.data.length) {
    const last = dataSource.data[dataSource.data.length - 1]
    if (last && typeof last.y !== 'undefined') return last.y
    if (last && typeof last.value !== 'undefined') return last.value
  }
  if (typeof dataSource.peek === 'function') {
    try { const v = dataSource.peek(); if (v !== undefined) return v } catch {}
  }
  if (typeof dataSource.get === 'function') {
    try { const v = dataSource.get(); if (v !== undefined) return v } catch {}
  }
  return undefined
}

/** Defensive wrapper providing the interface charts expect */
function makeSafeDataSource(original: any) {
  const noop = {
    subscribe: (_cb: any) => () => {},
    getLatest: () => undefined,
    hasCapability: (_cap?: any) => false,
    getRange: (_opts?: any) => [],
    isQueryableCollection: () => false,
  }
  if (!original) return noop

  const origHasCapability = typeof original.hasCapability === 'function'
    ? (cap: string) => !!original.hasCapability(cap)
    : (_cap: string) => {
        try {
          if (Array.isArray(original.capabilities)) return original.capabilities.includes(_cap as any)
          if (original.capabilities && typeof original.capabilities[_cap] !== 'undefined') return !!original.capabilities[_cap]
        } catch (e) {}
        return false
      }

  if (typeof original.subscribe === 'function') {
    return {
      subscribe: (cb: any) => {
        try {
          const unsub = original.subscribe(cb)
          if (typeof unsub === 'function') return unsub
          if (unsub && typeof unsub.unsubscribe === 'function') return () => unsub.unsubscribe()
        } catch (e) {
          console.warn('original.subscribe threw', e)
        }
        return () => {}
      },
      getLatest: () => getLatestFromDataSource(original),
      hasCapability: (cap: string) => {
        if (cap === 'subscribe') return true
        if (cap === 'getLatest') return typeof original.getLatest === 'function' || getLatestFromDataSource(original) !== undefined
        return origHasCapability(cap)
      },
      getRange: (opts?: any) => {
        if (typeof original.getRange === 'function') {
          try { return original.getRange(opts) } catch { return [] }
        }
        return []
      },
      isQueryableCollection: () => {
        try {
          if (typeof original.isQueryableCollection === 'function') return !!original.isQueryableCollection()
          if (original.isQueryableCollection !== undefined) return !!original.isQueryableCollection
        } catch (e) {}
        return false
      },
    }
  }

  // Polling wrapper
  return {
    subscribe: (cb: any) => {
      const intervalMs = 100
      let lastSeen: any = undefined
      const tid = window.setInterval(() => {
        const latest = getLatestFromDataSource(original)
        if (latest !== undefined && latest !== lastSeen) {
          lastSeen = latest
          try { cb(latest) } catch (e) {}
        }
      }, intervalMs)
      return () => { window.clearInterval(tid) }
    },
    getLatest: () => getLatestFromDataSource(original),
    hasCapability: (cap: string) => {
      if (cap === 'subscribe') return true
      if (cap === 'getLatest') return getLatestFromDataSource(original) !== undefined
      return origHasCapability(cap)
    },
    getRange: (_opts?: any) => [],
    isQueryableCollection: () => {
      try { if (original.isQueryableCollection !== undefined) return !!original.isQueryableCollection } catch (e) {}
      return false
    },
  }
}

export const OverviewPage = (props: RouteComponentProps) => {
  const ledStateDataSource = useMessageDataSource('led_state')
  const batteryEfficiencyDataSource = useMessageDataSource('battery')
  const speedDataSource = useMessageDataSource('speed')
  const distanceDataSource = useMessageDataSource('ultra')
  const rssiDataSource = useMessageDataSource('rssi')

  const deviceManager = useDeviceManager() as any
  const device =
    deviceManager.connectedDevices?.[0] ??
    deviceManager.devices?.[0] ??
    null

    // DEBUG: expose device to window so we can call write() manually from the console
    useEffect(() => {
  // @ts-ignore
  window._electricui_device = device;
  console.log('DEBUG: electricui device object:', device);
}, [device]);


  const [propulsionOnState, setPropulsionOnState] = useState<boolean>(false)
{/* 
  const handlePropulsionToggle = async () => {
    const next = !propulsionOnState
    setPropulsionOnState(next)
    if (device?.write) {
      try {
        await device.write({ propulsion: next ? 1 : 0 })
      } catch (err) {
        console.warn('device.write failed', err)
        setPropulsionOnState(!next)s
      }
    } else {
      console.warn('No device available to write propulsion state', next)
    }
  }*/}

  const handlePropulsionToggle = async () => {
  const next = !propulsionOnState;
  setPropulsionOnState(next);
  const propulsionValue = next ? 1 : 0;

  // Preferred path: electricui device.write
  if (device?.write) {
    try {
      await device.write({ propulsion: propulsionValue });
      console.log('device.write success -> propulsion=', propulsionValue);
      return;
    } catch (err) {
      console.warn('device.write failed', err);
      setPropulsionOnState(!next);
      return;
    }
  }

  // Fallback: Web Serial ASCII command "RELAY ON\n" / "RELAY OFF\n"
  const asciiCmd = propulsionValue ? 'RELAY ON\n' : 'RELAY OFF\n';

  if (!('serial' in navigator)) {
    console.warn('No Web Serial API available and no device.write -> cannot send command.');
    setPropulsionOnState(!next);
    return;
  }

  try {
    // Try to reuse existing granted port; otherwise, request one
    const webSerial: any = (navigator as any).serial;
    let ports = await webSerial.getPorts();
    let port = ports.length ? ports[0] : null;

    if (!port) {
      // prompt user to pick the port
      port = await webSerial.requestPort();
    }

    await port.open({ baudRate: 115200 });
    const writer = port.writable.getWriter();
    const enc = new TextEncoder();
    await writer.write(enc.encode(asciiCmd));
    await writer.close();
    await port.close();
    console.log('Wrote fallback serial command:', asciiCmd.trim());
  } catch (err) {
    console.warn('Fallback serial write failed', err);
    setPropulsionOnState(!next);
  }
}



  // Last distance numeric
  const [lastDistance, setLastDistance] = useState<number | null>(null)
  useEffect(() => {
    if (!distanceDataSource) return
    if (typeof distanceDataSource.subscribe === 'function') {
      try {
        const unsub = distanceDataSource.subscribe((v: any) => {
          const val = typeof v === 'number'
            ? v
            : (v && (v.value ?? v.y ?? v.latest ?? (Array.isArray(v.data) ? v.data[v.data.length - 1] : undefined))) ?? undefined
          if (typeof val === 'number') setLastDistance(val)
        })
        return () => { if (typeof unsub === 'function') unsub() }
      } catch (err) {
        console.warn('distanceDataSource.subscribe threw, falling back to poll', err)
      }
    }
    const pid = window.setInterval(() => {
      try {
        const latest = getLatestFromDataSource(distanceDataSource)
        if (typeof latest === 'number') setLastDistance(latest)
      } catch (e) {}
    }, 150)
    return () => window.clearInterval(pid)
  }, [distanceDataSource])

  // Last RSSI numeric
  const [lastRssi, setLastRssi] = useState<number | null>(null)
  useEffect(() => {
    if (!rssiDataSource) return
    if (typeof rssiDataSource.subscribe === 'function') {
      try {
        const unsub = rssiDataSource.subscribe((v: any) => {
          if (typeof v === 'number') {
            setLastRssi(v > 200 ? v - 256 : v)
          } else if (v && typeof v.value === 'number') {
            const n = v.value
            setLastRssi(n > 200 ? n - 256 : n)
          }
        })
        return () => { if (typeof unsub === 'function') unsub() }
      } catch (err) {
        console.warn('rssiDataSource.subscribe threw, falling back to poll', err)
      }
    }
    const pid = window.setInterval(() => {
      try {
        const latest = getLatestFromDataSource(rssiDataSource)
        if (typeof latest === 'number') setLastRssi(latest > 200 ? latest - 256 : latest)
      } catch (e) {}
    }, 200)
    return () => window.clearInterval(pid)
  }, [rssiDataSource])

  // Safe sources for charts
  const safeDistanceSource = useMemo(() => makeSafeDataSource(distanceDataSource), [distanceDataSource])
  const safeRssiSource = useMemo(() => makeSafeDataSource(rssiDataSource), [rssiDataSource])

  // debug logging (open DevTools -> Console)
  useEffect(() => {
    // eslint-disable-next-line no-console
    console.log('raw distanceDataSource:', distanceDataSource)
    // eslint-disable-next-line no-console
    console.log('wrapped safeDistanceSource:', safeDistanceSource)
    // eslint-disable-next-line no-console
    console.log('raw rssiDataSource:', rssiDataSource)
  }, [distanceDataSource, rssiDataSource, safeDistanceSource])

  return (
    <React.Fragment>
      <IntervalRequester interval={50} messageIDs={['led_state','battery','speed','ultra','rssi']} />

      <div style={gridStyle}>
        {/* Speed Chart */}
        <div style={{ gridColumn: '1 / 2' }}>
          <Card>
            <div style={{ textAlign:'center', marginBottom:8 }}><b>Speed</b></div>
            <ChartContainer>
              <LineChart key="speed" dataSource={speedDataSource} />
              <RealTimeDomain window={10000} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
          </Card>
        </div>

        {/* Battery Chart */}
        <div style={{ gridColumn: '2 / 3' }}>
          <Card>
            <div style={{ textAlign:'center', marginBottom:8 }}><b>Battery Efficiency</b></div>
            <ChartContainer>
              <LineChart key="battery" dataSource={batteryEfficiencyDataSource} />
              <RealTimeDomain window={10000} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
          </Card>
        </div>

                {/* Distance Chart */}
        <div style={{ gridColumn: '3 / 3' }}>
          <Card>
            <div style={{ textAlign: 'center', marginBottom: 8 }}>
              <b>Ultrasonic Distance (mm)</b>
            </div>

            <ChartContainer>
              <LineChart key="distance" dataSource={safeDistanceSource} />
              <RealTimeDomain window={15000} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>

            {/* Live numeric readout */}
            <div style={{ padding: 12, textAlign: 'center' }}>
              <div style={{ fontSize: 12, color: '#666' }}>Latest Value</div>
              <div style={{ fontSize: 24, fontWeight: 600 }}>
                {lastDistance === null ? '—' : `${lastDistance.toFixed(0)} mm`}
              </div>
            </div>
          </Card>
        </div>

        {/* Slider */}
        <div style={{ gridColumn: '1 / 2' }}>
          <Card>
            <div style={{ margin:12 }}>
              <div style={{ marginBottom:8 }}>Transmission Frequency (ms)</div>
              <Slider min={20} max={120} stepSize={5} labelStepSize={5} sendOnlyOnRelease>
                <Slider.Handle accessor="lit_time" />
              </Slider>
            </div>
          </Card>
        </div>

       {/*}
        <div style={{ gridColumn: '2 / 3', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <Card style={{ width: '100%', minHeight: '80px', alignSelf: 'center', height: '100%' }}>
          <div style={{ padding: 12, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
             <Button large intent={propulsionOnState ? 'success' : 'primary'} text={propulsionOnState ? 'TURN RELAY OFF' : 'TURN RELAY ON'} onClick={handlePropulsionToggle} />
          </div>
          </Card>
        </div>

        {/* Distance stat 
        <div style={{ gridColumn: '3 / 3' }}>
          <Card>
            <div style={{ padding:12 }}>
              <div style={{ fontSize:12, color:'#666' }}>Last Distance</div>
              <div style={{ fontSize:20 }}>{lastDistance === null ? '—' : `${lastDistance} mm`}</div>
            </div>
          </Card>
        </div>*/}

        {/* Propulsion Voltage (wide) 
        <div style={{ gridColumn: '1 / 2' }}>
          <Card>
            <div style={{ padding:12 }}>
              <div style={{ fontSize:12, color:'#666' }}>Propulsion Voltage</div>
              {/* Using Statistic accessor as a display; it will work if 'voltage' is tracked 
              <div style={{ marginTop:8 }}><Statistic accessor="voltage" label="" suffix="V" /></div>
            </div>
          </Card>
        </div> */} 

                {/* Propulsion Voltage (middle column, with embedded button) */}
        <div style={{ gridColumn: '2 / 3', gridRow: '2 / 3', display: 'flex' }}>
          <Card style={{width: '100%', height: '130px', display: 'flex', alignItems: 'stretch', boxSizing: 'border-box'}}>
            <div style={{ gridColumn: '2 / 3', gridRow: '2 / 3', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
              {/* Left side: Propulsion button */}
              <div style={{ width: '70%', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 12, borderRight: '1px solid rgba(255,255,255,0.1)' }}>
                <Button
                  large
                  intent={propulsionOnState ? 'success' : 'primary'}
                  text={propulsionOnState ? 'TURN RELAY OFF' : 'TURN RELAY ON'}
                  onClick={handlePropulsionToggle}
                />
              </div>
              {/* Right side: Voltage display */}
              <div style={{ width: '30%', padding: 16, display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
                {/*<div style={{ fontSize: 13, fontWeight: 600, color: '#ffffffff', marginBottom: 4 }}>Propulsion Voltage</div> */}
                <div style={{ fontSize: 40, marginTop: 8 }}>
                  <Statistic accessor="voltage" label="" suffix="V" formatter={(value: number) => value?.toFixed(2)}/>
                </div>
              </div>
            </div>
          </Card>
        </div>

        

        {/* RSSI Chart + numeric */}
        <div style={{ gridColumn: '3 / 4' }}>
          <Card>
            <div style={{ textAlign:'center', marginBottom:8 }}><b>RSSI (dBm)</b></div>
            <ChartContainer>
              <LineChart key="rssi" dataSource={safeRssiSource} />
              <RealTimeDomain window={15000} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
            <div style={{ padding:12, textAlign:'center' }}>
              <div style={{ fontSize:12, color:'#666' }}>Last RSSI</div>
              <div style={{ fontSize:20 }}>{lastRssi === null ? '—' : `${lastRssi} dBm`}</div>
            </div>
          </Card>
        </div>

      </div>
    </React.Fragment>
  )
}

export default OverviewPage
