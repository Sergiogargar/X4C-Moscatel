/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: {
        mono: ['"JetBrains Mono"', '"Fira Code"', 'ui-monospace', 'monospace'],
        sans: ['"Inter"', 'ui-sans-serif', 'system-ui', 'sans-serif'],
      },
      colors: {
        // Colores semáforo para vibración
        vibration: {
          ok:       '#10b981', // Verde
          warning:  '#f59e0b', // Ámbar
          critical: '#ef4444', // Rojo
        },
        // Superficie del dashboard (modo oscuro)
        surface: {
          900: '#0a0e17',
          800: '#111827',
          700: '#1e293b',
          600: '#334155',
        },
      },
    },
  },
  plugins: [],
}
