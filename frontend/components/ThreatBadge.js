import { jsx as _jsx } from "react/jsx-runtime";
export function ThreatBadge({ severity }) {
    return (_jsx("span", { className: `hx-badge hx-badge--${severity}`, children: severity }));
}
