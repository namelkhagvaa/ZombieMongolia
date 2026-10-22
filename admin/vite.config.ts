import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Серверт https://play-zombie.apps.mn/admin/ дээр (nginx: admin/dist), API нь ижил домэйны /api/.
// Хөгжүүлэлтэд /api/-г Django dev сервер (127.0.0.1:8010) рүү дамжуулна.
export default defineConfig({
  base: '/admin/',
  plugins: [react()],
  server: {
    port: 5173,
    proxy: { '/api': 'http://127.0.0.1:8010' },
  },
  build: {
    chunkSizeWarningLimit: 1200,
    rollupOptions: {
      output: {
        manualChunks: {
          react: ['react', 'react-dom', 'react-router-dom'],
          antd: ['antd', '@ant-design/icons'],
          charts: ['recharts'],
        },
      },
    },
  },
});
