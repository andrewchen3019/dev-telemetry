// OverviewPage.tsx
import React, { useState, useEffect, useMemo } from 'react'
import { RouteComponentProps } from '@reach/router'

import {
  ChartContainer,
  LineChart,
  RealTimeDomain,
  TimeAxis,
  VerticalAxis,
} from '@electricui/components-desktop-charts'

import { Card } from '@blueprintjs/core'
import { IntervalRequester } from '@electricui/components-core'
import { useMessageDataSource } from '@electricui/core-timeseries'
import { Slider, Switch } from '@electricui/components-desktop-blueprint'

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

const cardFullStyle: React.CSSProperties = {
  height: '100%',
  flexDirection: 'column',
  boxSizing: 'border-box',
  textAlign: 'center',
}

const cellStyle: React.CSSProperties = {}

function getLatestFromDataSource(dataSource: any): number | undefined {
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
    : (_cap: string) => false

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
      isQueryableCollection: () => false,
    }
  }

  // Polling wrapper fallback
  return {
    subscribe: (cb: any) => {
      let lastSeen: any = undefined
      const tid = window.setInterval(() => {
        const latest = getLatestFromDataSource(original)
        if (latest !== undefined && latest !== lastSeen) {
          lastSeen = latest
          try { cb(latest) } catch (e) {}
        }
      }, 100)
      return () => { window.clearInterval(tid) }
    },
    getLatest: () => getLatestFromDataSource(original),
    hasCapability: (cap: string) => cap === 'subscribe' || cap === 'getLatest',
    getRange: (_opts?: any) => [],
    isQueryableCollection: () => false,
  }
}

// Hook to poll a single data source into a numeric state value
function useLatestValue(dataSource: any, intervalMs = 150): number | null {
  const [value, setValue] = useState<number | null>(null)
  useEffect(() => {
    if (!dataSource) return
    if (typeof dataSource.subscribe === 'function') {
      try {
        const unsub = dataSource.subscribe((v: any) => {
          const n = typeof v === 'number' ? v : (v?.value ?? v?.y ?? undefined)
          if (typeof n === 'number') setValue(n)
        })
        return () => { if (typeof unsub === 'function') unsub() }
      } catch {}
    }
    const pid = window.setInterval(() => {
      const latest = getLatestFromDataSource(dataSource)
      if (typeof latest === 'number') setValue(latest)
    }, intervalMs)
    return () => window.clearInterval(pid)
  }, [dataSource])
  return value
}

// Hook to reconstruct a 32-bit value from hi/lo data sources
function useReconstructed32(loSource: any, hiSource: any, intervalMs = 150): number | null {
  const [value, setValue] = useState<number | null>(null)
  useEffect(() => {
    if (!loSource || !hiSource) return
    const pid = window.setInterval(() => {
      const lo = getLatestFromDataSource(loSource)
      const hi = getLatestFromDataSource(hiSource)
      if (typeof lo === 'number' && typeof hi === 'number') {
        setValue(((hi << 16) | lo) >>> 0)
      }
    }, intervalMs)
    return () => window.clearInterval(pid)
  }, [loSource, hiSource])
  return value
}

export const OverviewPage = (props: RouteComponentProps) => {
  // Data sources
  const motorRpmLoSource      = useMessageDataSource('motor_rpm_lo')
  const motorRpmHiSource      = useMessageDataSource('motor_rpm_hi')
  const throttleSource        = useMessageDataSource('throttle_pct')
  const joulemeterLoSource    = useMessageDataSource('joulemeter_lo')
  const joulemeterHiSource    = useMessageDataSource('joulemeter_hi')
  const joulemeterCurrentSrc  = useMessageDataSource('joulemeter_current')
  const joulemeterVoltageSrc  = useMessageDataSource('joulemeter_voltage')

  // Propulsion sources (kept for functionality, not displayed)
  const propulsionStateSource = useMessageDataSource('propulsion_state')

  // Numeric values
  const rawRpm        = useReconstructed32(motorRpmLoSource, motorRpmHiSource)
  const lastRpm       = rawRpm !== null ? rawRpm / 1000 : null           // true RPM

  const rawThrottle   = useLatestValue(throttleSource)
  const lastThrottle  = rawThrottle !== null ? rawThrottle / 10 : null   // true %

  const rawEnergy     = useReconstructed32(joulemeterLoSource, joulemeterHiSource)
  const lastEnergy    = rawEnergy !== null ? rawEnergy / 1000 : null     // joules

  const rawCurrent    = useLatestValue(joulemeterCurrentSrc)
  const lastCurrent   = rawCurrent !== null ? rawCurrent / 1000 : null   // amps

  const rawVoltage    = useLatestValue(joulemeterVoltageSrc)
  const lastVoltage   = rawVoltage !== null ? rawVoltage / 1000 : null   // volts

  // Safe chart sources
  const safeRpmSource      = useMemo(() => makeSafeDataSource(motorRpmLoSource), [motorRpmLoSource])
  const safeThrottleSource = useMemo(() => makeSafeDataSource(throttleSource), [throttleSource])
  const safeEnergySource   = useMemo(() => makeSafeDataSource(joulemeterLoSource), [joulemeterLoSource])
  const safeCurrentSource  = useMemo(() => makeSafeDataSource(joulemeterCurrentSrc), [joulemeterCurrentSrc])
  const safeVoltageSource  = useMemo(() => makeSafeDataSource(joulemeterVoltageSrc), [joulemeterVoltageSrc])

  return (
    <React.Fragment>
      <IntervalRequester
        interval={50}
        messageIDs={[
          'motor_rpm_lo', 'motor_rpm_hi',
          'throttle_pct',
          'joulemeter_lo', 'joulemeter_hi',
          'joulemeter_current', 'joulemeter_voltage',
          'propulsion_state',
        ]}
      />

      <div style={gridStyle}>

        {/* RPM Chart */}
        <div style={cellStyle}>
          <Card style={cardFullStyle}>
            <div style={{ padding: 12 }}><b>Motor RPM</b></div>
            <ChartContainer style={{ flex: 1, minHeight: 0 }}>
              <LineChart key="rpm" dataSource={safeRpmSource} />
              <RealTimeDomain window={15000} yMin={0} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
            <div style={{ padding: 12, textAlign: 'center' }}>
              <div style={{ fontSize: 12, color: '#666' }}>Latest</div>
              <div style={{ fontSize: 24, fontWeight: 600 }}>
                {lastRpm === null ? '—' : `${lastRpm.toFixed(0)} RPM`}
              </div>
            </div>
          </Card>
        </div>

        {/* Throttle Chart */}
        <div style={cellStyle}>
          <Card style={cardFullStyle}>
            <div style={{ padding: 12 }}><b>Throttle</b></div>
            <ChartContainer style={{ flex: 1, minHeight: 0 }}>
              <LineChart key="throttle" dataSource={safeThrottleSource} />
              <RealTimeDomain window={15000} yMin={0} yMaxSoft={100} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
            <div style={{ padding: 12, textAlign: 'center' }}>
              <div style={{ fontSize: 12, color: '#666' }}>Latest</div>
              <div style={{ fontSize: 24, fontWeight: 600 }}>
                {lastThrottle === null ? '—' : `${lastThrottle.toFixed(1)} %`}
              </div>
            </div>
          </Card>
        </div>

        {/* Energy Chart */}
        <div style={cellStyle}>
          <Card style={cardFullStyle}>
            <div style={{ padding: 12 }}><b>Accumulated Energy</b></div>
            <ChartContainer style={{ flex: 1, minHeight: 0 }}>
              <LineChart key="energy" dataSource={safeEnergySource} />
              <RealTimeDomain window={15000} yMin={0} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
            <div style={{ padding: 12, textAlign: 'center' }}>
              <div style={{ fontSize: 12, color: '#666' }}>Latest</div>
              <div style={{ fontSize: 24, fontWeight: 600 }}>
                {lastEnergy === null ? '—' : `${lastEnergy.toFixed(2)} J`}
              </div>
            </div>
          </Card>
        </div>

        {/* Current Chart */}
        <div style={cellStyle}>
          <Card style={cardFullStyle}>
            <div style={{ padding: 12 }}><b>Current</b></div>
            <ChartContainer style={{ flex: 1, minHeight: 0 }}>
              <LineChart key="current" dataSource={safeCurrentSource} />
              <RealTimeDomain window={15000} yMin={0} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
            <div style={{ padding: 12, textAlign: 'center' }}>
              <div style={{ fontSize: 12, color: '#666' }}>Latest</div>
              <div style={{ fontSize: 24, fontWeight: 600 }}>
                {lastCurrent === null ? '—' : `${lastCurrent.toFixed(3)} A`}
              </div>
            </div>
          </Card>
        </div>

        {/* Voltage Chart */}
        <div style={cellStyle}>
          <Card style={cardFullStyle}>
            <div style={{ padding: 12 }}><b>Voltage</b></div>
            <ChartContainer style={{ flex: 1, minHeight: 0 }}>
              <LineChart key="voltage" dataSource={safeVoltageSource} />
              <RealTimeDomain window={15000} yMin={0} />
              <TimeAxis />
              <VerticalAxis />
            </ChartContainer>
            <div style={{ padding: 12, textAlign: 'center' }}>
              <div style={{ fontSize: 12, color: '#666' }}>Latest</div>
              <div style={{ fontSize: 24, fontWeight: 600 }}>
                {lastVoltage === null ? '—' : `${lastVoltage.toFixed(3)} V`}
              </div>
            </div>
          </Card>
        </div>

        {/* Transmission frequency slider — kept as a useful control */}
        <div style={cellStyle}>
          <Card style={cardFullStyle}>
            <div style={{ margin: 12 }}>
              <div style={{ marginBottom: 8 }}>Transmission Frequency (ms)</div>
              <Slider min={20} max={250} stepSize={5} labelStepSize={20} sendOnlyOnRelease>
                <Slider.Handle accessor="lit_time" />
              </Slider>
            </div>
          </Card>
        </div>

      </div>

      {/* Propulsion switch — hidden from view but kept in DOM so ElectricUI state is maintained */}
      <div style={{ display: 'none' }}>
        <Switch
          unchecked={0}
          checked={1}
          accessor={state => state.led_blink}
          writer={(state, value) => { state.led_blink = value }}
        >
          Toggle propulsion
        </Switch>
      </div>

    </React.Fragment>
  )
}