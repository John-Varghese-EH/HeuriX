import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';
export default defineConfig({
    plugins: [react()],
    root: '.',
    publicDir: 'public',
    resolve: {
        alias: {
            '@': path.resolve(__dirname, './frontend'),
        },
    },
    build: {
        outDir: 'dist',
        emptyOutDir: true,
        sourcemap: false,
        minify: 'esbuild',
        target: 'esnext',
    },
    server: {
        port: 1420,
        strictPort: true,
        watch: {
            ignored: ['**/src-tauri/**'],
        },
    },
    css: {
        devSourcemap: true,
    },
});
