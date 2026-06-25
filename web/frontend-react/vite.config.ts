import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

//! Dev server proxies /api to the NestJS BFF (same convention as the Angular app's
//! proxy.conf.json), so the React POC hits the exact same backend with no CORS setup.
//! Override the target with VITE_API_TARGET when the BFF runs elsewhere.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 4300,
    proxy: {
      '/api': {
        target: process.env.VITE_API_TARGET ?? 'http://localhost:3000',
        changeOrigin: true,
        secure: false,
      },
    },
  },
});
