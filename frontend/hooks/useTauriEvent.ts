import { useEffect } from 'react';
import { listen, UnlistenFn } from '@tauri-apps/api/event';

export function useTauriEvent<T>(eventName: string, handler: (payload: T) => void) {
  useEffect(() => {
    let unlisten: UnlistenFn;
    listen<T>(eventName, (event) => handler(event.payload))
      .then((fn) => { unlisten = fn; });
    return () => { if (unlisten) unlisten(); };
  }, [eventName]);
}
