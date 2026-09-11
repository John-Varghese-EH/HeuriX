

interface ThreatBadgeProps {
  severity: 'info' | 'low' | 'medium' | 'high' | 'critical';
}

export function ThreatBadge({ severity }: ThreatBadgeProps) {
  return (
    <span className={`hx-badge hx-badge--${severity}`}>
      {severity}
    </span>
  );
}
