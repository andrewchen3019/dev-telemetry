import 'source-map-support/register'

import React from 'react'
import { createRoot } from 'react-dom/client'
import { Root } from './Root'
import { setupDarkModeListenersRenderer } from '@electricui/utility-electron'

setupDarkModeListenersRenderer()

const container = document.createElement('div')
container.className = 'root'
document.body.appendChild(container)

function render(Component: React.FC) {
  const root = createRoot(container) // createRoot(container!) if you use TypeScript
  root.render(<Component />)
}

render(Root)
