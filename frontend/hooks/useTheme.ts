import { useState, useEffect } from 'react';
import { getCurrentWindow } from '@tauri-apps/api/window';

type Theme = 'system' | 'light' | 'dark';

export function useTheme() {
  const [theme, setTheme] = useState<Theme>(() => {
    return (localStorage.getItem('hx-theme') as Theme) || 'system';
  });

  useEffect(() => {
    localStorage.setItem('hx-theme', theme);
    const root = document.documentElement;

    // Sync Tauri window decorations (titlebar) with the React app theme
    try {
      getCurrentWindow().setTheme(theme === 'system' ? null : theme).catch(() => {});
    } catch (e) {
      // Ignore if not running in Tauri (e.g. browser preview)
    }

    if (theme === 'system') {
      const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
      root.setAttribute('data-theme', mediaQuery.matches ? 'dark' : 'light');
      
      const listener = (e: MediaQueryListEvent) => {
        root.setAttribute('data-theme', e.matches ? 'dark' : 'light');
      };
      
      mediaQuery.addEventListener('change', listener);
      return () => mediaQuery.removeEventListener('change', listener);
    } else {
      root.setAttribute('data-theme', theme);
    }
  }, [theme]);

  return { theme, setTheme };
}
