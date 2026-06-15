import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    // Proxy /influx → InfluxDB local para evitar CORS en desarrollo
    proxy: {
      '/influx': {
        target: 'http://localhost:8086',
        changeOrigin: true,
        rewrite: (p) => p.replace(/^\/influx/, ''),
      },
    },
  },
})
