// src/services/mqttClient.ts
/**
 * Runtime-loaded browser MQTT client via unpkg CDN.
 * This avoids bundler resolution issues under Yarn Berry / PnP.
 *
 * Usage:
 *   import { startMQTT, publish, stopMQTT, isConnected } from 'services/mqttClient';
 *   const client = await startMQTT(cb);
 *   publish(TOPIC_RX, { propulsion: 1 });
 *   stopMQTT();
 */

import type { MqttClient } from 'mqtt';

export type Telemetry = {
  battery?: number;
  speed?: number;
  [k: string]: any;
};

export const TOPIC_TELEMETRY = 'my_project/esp32_01/telemetry';
export const TOPIC_RX = 'my_project/esp32_01/rx';
export const TOPIC_TX = 'my_project/esp32_01/tx';

// Prefer Vite vars, fallback to CRA/webpack style
const _env: any =
  typeof import.meta !== 'undefined' && (import.meta as any).env
    ? (import.meta as any).env
    : (typeof process !== 'undefined' ? process.env : {});

const HOST = (_env.VITE_MQTT_HOST || _env.REACT_APP_MQTT_HOST || 'YOUR_CLUSTER_HOST') as string;
const PORT = (_env.VITE_MQTT_PORT || _env.REACT_APP_MQTT_PORT || '8884') as string;
const USER = (_env.VITE_MQTT_USER || _env.REACT_APP_MQTT_USER || '') as string;
const PASS = (_env.VITE_MQTT_PASS || _env.REACT_APP_MQTT_PASS || '') as string;

const URL = `wss://${HOST}:${PORT}/mqtt`;

/**
 * Load mqtt browser bundle from CDN (only once).
 * Returns the mqtt library object (the same as `require('mqtt')` in browser builds).
 */
async function loadMqttFromCdn(): Promise<any> {
  // @ts-ignore - window may gain mqtt
  if (typeof window !== 'undefined' && (window as any).mqtt) {
    // already present (maybe included via <script> in index.html)
    return (window as any).mqtt;
  }

  // If running in non-browser (SSR), throw
  if (typeof document === 'undefined') {
    throw new Error('loadMqttFromCdn: document is undefined (not running in browser)');
  }

  return new Promise((resolve, reject) => {
    const existing = document.querySelector('script[data-mqtt-cdn="true"]') as HTMLScriptElement | null;
    if (existing) {
      // If the script element exists but mqtt not yet available, wait for load or error events
      if ((window as any).mqtt) return resolve((window as any).mqtt);
      existing.addEventListener('load', () => resolve((window as any).mqtt));
      existing.addEventListener('error', (e) => reject(new Error('Failed to load mqtt from CDN')));
      return;
    }

    const script = document.createElement('script');
    script.setAttribute('data-mqtt-cdn', 'true');
    // Use unpkg CDN — pinned to the dist browser bundle
    // You may choose to pin to a specific mqtt version (e.g. /mqtt@5.14.1/...)
    script.src = 'https://unpkg.com/mqtt/dist/mqtt.min.js';
    script.async = true;
    script.onload = () => {
      // @ts-ignore
      if ((window as any).mqtt) {
        resolve((window as any).mqtt);
      } else {
        reject(new Error('MQTT loaded but global mqtt not found'));
      }
    };
    script.onerror = () => reject(new Error('Failed to load mqtt from CDN'));
    document.head.appendChild(script);
  });
}

// Mutable client reference
let client: MqttClient | null = null;

/**
 * startMQTT: connect to broker and subscribe to telemetry.
 * Returns a Promise that resolves to the client instance.
 */
export async function startMQTT(onTelemetry: (msg: Telemetry) => void = () => {}): Promise<MqttClient> {
  const mqttLib = await loadMqttFromCdn();
  // mqttLib.connect(...) returns a client in browser
  if (client && client.connected) return client;

  const options = {
    clientId: `webui_${Math.random().toString(16).slice(2, 10)}`,
    username: USER || undefined,
    password: PASS || undefined,
    keepalive: 30,
    reconnectPeriod: 2000,
    clean: true,
    connectTimeout: 30 * 1000,
  };

  client = mqttLib.connect(URL, options) as MqttClient;

  client.on('connect', () => {
    try {
      client!.subscribe(TOPIC_TELEMETRY, { qos: 0 }, (err: Error | null) => {
        if (err) {
          // subscription error; log and continue
          // eslint-disable-next-line no-console
          console.warn('MQTT subscribe error', err);
        }
      });
    } catch (e) {
      // eslint-disable-next-line no-console
      console.warn('Subscribe call failed', e);
    }
  });

  client.on('message', (topic: string, payload: Buffer) => {
    if (topic === TOPIC_TELEMETRY) {
      try {
        const parsed = JSON.parse(payload.toString()) as Telemetry;
        onTelemetry(parsed);
      } catch (err) {
        // eslint-disable-next-line no-console
        console.error('Failed to parse telemetry JSON', err, payload.toString());
      }
    } else {
      // eslint-disable-next-line no-console
      console.debug('MQTT message', topic, payload.toString());
    }
  });

  client.on('reconnect', () => {
    // eslint-disable-next-line no-console
    console.log('MQTT reconnecting...');
  });

  client.on('close', () => {
    // eslint-disable-next-line no-console
    console.log('MQTT closed');
  });

  client.on('error', (err: any) => {
    // eslint-disable-next-line no-console
    console.error('MQTT error', err);
  });

  return client!;
}

/** Publish helper */
export function publish(topic: string, data: any): void {
  if (!client || !client.connected) {
    // eslint-disable-next-line no-console
    console.warn('MQTT not connected; cannot publish', topic);
    return;
  }
  const payload = typeof data === 'object' ? JSON.stringify(data) : String(data);
  client.publish(topic, payload);
}

/** Gracefully stop client */
export function stopMQTT(): void {
  if (client) {
    try {
      client.end(true);
    } catch (e) {
      // eslint-disable-next-line no-console
      console.warn('Error while ending MQTT client', e);
    } finally {
      client = null;
    }
  }
}

/** Check connection */
export function isConnected(): boolean {
  return !!(client && client.connected);
}
