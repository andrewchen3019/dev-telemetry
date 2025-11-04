import React from 'react'
import { useHardwareState } from '@electricui/components-core'

export const Data = () =>  {
  // these strings must match the message IDs you used in the embedded tracked vars
  const speed = useHardwareState<number>('vehicle_speed_kmh') ?? 0
  const eff = useHardwareState<number>('battery_efficiency_wh_per_km') ?? 0
  const voltage = useHardwareState<number>('battery_voltage_v') ?? 0
  const current = useHardwareState<number>('battery_current_a') ?? 0

    // compute Wh/km:
  // If speed is km/h: efficiency Wh/km = (V * I) / (km/h)
  // handle zero speed
  const efficiency = useMemo(() => {
    if (speed <= 0.1) return 0
    const power_w = voltage * current
    return power_w / speed // Wh per km
  }, [voltage, current, speed])


  return (
    <div className="telemetry-card">
      <h3>Battery & Speed</h3>
      <div>Speed: {speed.toFixed(1)} km/h</div>
      <div>Voltage: {voltage.toFixed(2)} V</div>
      <div>Current: {current.toFixed(2)} A</div>
      <div>
        Efficiency: {eff > 0 ? eff.toFixed(2) : '—'} Wh / km
      </div>
    </div>
  )
}