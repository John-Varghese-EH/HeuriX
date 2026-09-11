import { useEffect } from 'react';
import { listen } from '@tauri-apps/api/event';
export function useTauriEvent(eventName, handler) {
    useEffect(() => {
        let unlisten;
        listen(eventName, (event) => handler(event.payload))
            .then((fn) => { unlisten = fn; });
        return () => { if (unlisten)
            unlisten(); };
    }, [eventName]);
}
