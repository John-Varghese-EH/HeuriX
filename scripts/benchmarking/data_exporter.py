#!/usr/bin/env python3
"""
Research Paper Data Exporter & Visualization Formatter
======================================================
Processes SAFARI telemetry JSON and benchmark results to produce IEEE publication-quality
visualizations and formatted datasets:

1. Proportional Doughnut Charts (Summary percentages broken down by folder and extension)
2. File-System-Tree Profiles (Hierarchical network graphs with node size = file count, RGB = damage)
3. Format Spectrum Profiles (Additive RGB bar charts for attack severity across extensions)
4. ROC Curves & Confusion Matrices for False Positive & Latency analysis.
"""

import json
import math
import os
from pathlib import Path
from typing import Dict, List, Any, Optional

# Optional scientific visualization stack imports with graceful fallback
try:
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

try:
    import networkx as nx
    HAS_NETWORKX = True
except ImportError:
    HAS_NETWORKX = False

try:
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False

try:
    import numpy as np
    from sklearn.metrics import roc_curve, auc, confusion_matrix, ConfusionMatrixDisplay
    HAS_SKLEARN = True
except ImportError:
    HAS_SKLEARN = False


if HAS_MATPLOTLIB:
    plt.style.use('seaborn-v0_8-paper' if 'seaborn-v0_8-paper' in plt.style.available else 'default')
    plt.rcParams.update({
        'font.family': 'serif',
        'font.size': 10,
        'axes.labelsize': 11,
        'axes.titlesize': 12,
        'xtick.labelsize': 9,
        'ytick.labelsize': 9,
        'legend.fontsize': 9,
        'figure.titlesize': 13,
        'figure.dpi': 300
    })


class ResearchDataExporter:
    """Export benchmark telemetry to JSON/CSV and render publication figures."""

    def __init__(self, output_dir: str = "./benchmark_results"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def export_json(self, data: Dict[str, Any], filename: str = "safari_telemetry.json") -> Path:
        """Exports structured SAFARI telemetry matching IEEE dataset specs."""
        filepath = self.output_dir / filename
        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)
        print(f"[+] Exported JSON telemetry to {filepath}")
        return filepath

    def export_csv(self, rows: List[Dict[str, Any]], filename: str) -> Path:
        """Exports dataset to CSV format."""
        filepath = self.output_dir / filename
        if not rows:
            return filepath
        
        headers = list(rows[0].keys())
        with open(filepath, "w") as f:
            f.write(",".join(headers) + "\n")
            for r in rows:
                line = ",".join(str(r.get(h, "")) for h in headers)
                f.write(line + "\n")

        print(f"[+] Exported CSV dataset to {filepath}")
        return filepath

    # A. Proportional Doughnut Charts
    def plot_proportional_doughnuts(self, safari_data: Dict[str, Any], save_name: str = "fig1_doughnut_summary.png"):
        if not HAS_MATPLOTLIB:
            print("[!] Matplotlib not installed; saving raw vector SVG fallback...")
            self._generate_svg_doughnut(safari_data, save_name.replace(".png", ".svg"))
            return

        meta = safari_data.get("metadata", {})
        folder_summary = safari_data.get("folder_summary", {})
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

        labels_state = ['Pristine', 'Lost (Encrypted)', 'Replica']
        counts_state = [meta.get("pristine_count", 0), meta.get("lost_count", 0), meta.get("replica_count", 0)]
        colors_state = ['#2ecc71', '#e74c3c', '#3498db']

        ax1.pie(
            counts_state,
            labels=labels_state,
            colors=colors_state,
            autopct='%1.1f%%',
            startangle=140,
            pctdistance=0.75,
            wedgeprops=dict(width=0.4, edgecolor='w', linewidth=2)
        )
        ax1.set_title("Global File State Ratio (SAFARI)", fontsize=12, fontweight='bold')

        folders = list(folder_summary.keys())
        folder_lost = [folder_summary[f].get("lost", 0) for f in folders]

        if sum(folder_lost) > 0 and HAS_MATPLOTLIB:
            folder_colors = plt.cm.Oranges(np.linspace(0.4, 0.9, len(folders)))
            ax2.pie(
                folder_lost,
                labels=folders,
                colors=folder_colors,
                autopct='%1.1f%%',
                startangle=90,
                pctdistance=0.75,
                wedgeprops=dict(width=0.4, edgecolor='w', linewidth=2)
            )
            ax2.set_title("Damage Distribution by Folder (Lost Files)", fontsize=12, fontweight='bold')
        else:
            ax2.text(0.5, 0.5, "100% Protection (0 Lost Files)", horizontalalignment='center', verticalalignment='center')
            ax2.axis('off')

        plt.tight_layout()
        save_path = self.output_dir / save_name
        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        plt.close()
        print(f"[+] Saved Doughnut Chart to {save_path}")

    # B. File-System-Tree Profiles
    def plot_filesystem_tree(self, safari_data: Dict[str, Any], save_name: str = "fig2_filesystem_tree.png"):
        if not (HAS_MATPLOTLIB and HAS_NETWORKX):
            print("[!] NetworkX / Matplotlib not installed; skipping network graph PNG generation.")
            return

        folder_summary = safari_data.get("folder_summary", {})
        G = nx.DiGraph()
        G.add_node("root")

        node_sizes = [1500]
        node_colors = [(0.2, 0.8, 0.2)]
        labels = {"root": "Target Root"}

        for folder, counts in folder_summary.items():
            if folder == "root":
                continue
            pristine = counts.get("pristine", 0)
            lost = counts.get("lost", 0)
            replica = counts.get("replica", 0)
            total = pristine + lost + replica
            if total == 0:
                continue

            r = min(1.0, lost / total)
            g = min(1.0, pristine / total)
            b = min(1.0, replica / total)

            G.add_edge("root", folder)
            node_sizes.append(max(300, total * 50))
            node_colors.append((r, g, b))
            labels[folder] = f"{folder}\n({total} files)"

        fig, ax = plt.subplots(figsize=(10, 7))
        pos = nx.spring_layout(G, seed=42)

        nx.draw_networkx_nodes(G, pos, node_size=node_sizes, node_color=node_colors, alpha=0.9, ax=ax)
        nx.draw_networkx_edges(G, pos, width=2, edge_color='#bdc3c7', arrows=True, ax=ax)
        nx.draw_networkx_labels(G, pos, labels=labels, font_size=8, font_weight='bold', ax=ax)

        ax.set_title("File-System-Tree Profile (RGB Damage Severity Graph)", fontsize=12, fontweight='bold')
        ax.axis('off')

        plt.tight_layout()
        save_path = self.output_dir / save_name
        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        plt.close()
        print(f"[+] Saved File-System Tree Profile to {save_path}")

    # C. Format Spectrum Profiles
    def plot_format_spectrum(self, safari_data: Dict[str, Any], save_name: str = "fig3_format_spectrum.png"):
        if not HAS_MATPLOTLIB:
            print("[!] Matplotlib not installed; skipping format spectrum bar chart PNG.")
            return

        format_summary = safari_data.get("format_summary", {})
        extensions = list(format_summary.keys())
        if not extensions:
            return

        pristine = [format_summary[ext].get("pristine", 0) for ext in extensions]
        lost = [format_summary[ext].get("lost", 0) for ext in extensions]
        replica = [format_summary[ext].get("replica", 0) for ext in extensions]

        x = np.arange(len(extensions))
        width = 0.25

        fig, ax = plt.subplots(figsize=(11, 5))
        ax.bar(x - width, pristine, width, label='Pristine', color='#2ecc71')
        ax.bar(x, lost, width, label='Lost (Encrypted)', color='#e74c3c')
        ax.bar(x + width, replica, width, label='Replica', color='#3498db')

        ax.set_xlabel('File Extensions / Formats', fontweight='bold')
        ax.set_ylabel('File Count', fontweight='bold')
        ax.set_title('Format Spectrum Profile: Attack Severity across Extensions', fontsize=12, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(extensions, rotation=45)
        ax.legend()
        ax.grid(axis='y', linestyle='--', alpha=0.5)

        plt.tight_layout()
        save_path = self.output_dir / save_name
        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        plt.close()
        print(f"[+] Saved Format Spectrum Profile to {save_path}")

    # D. ROC Curve & Confusion Matrix
    def plot_roc_and_confusion_matrix(
        self,
        y_true: List[int],
        y_scores: List[float],
        save_name: str = "fig4_roc_confusion_matrix.png"
    ):
        if not (HAS_MATPLOTLIB and HAS_SKLEARN):
            print("[!] scikit-learn / Matplotlib not installed; skipping ROC curve calculation.")
            return

        fpr, tpr, _ = roc_curve(y_true, y_scores)
        roc_auc = auc(fpr, tpr)

        y_pred = [1 if s >= 0.5 else 0 for s in y_scores]
        cm = confusion_matrix(y_true, y_pred)

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))

        ax1.plot(fpr, tpr, color='#9b59b6', lw=2, label=f'HeuriX Classifier (AUC = {roc_auc:.3f})')
        ax1.plot([0, 1], [0, 1], color='#7f8c8d', lw=1.5, linestyle='--')
        ax1.set_xlim([0.0, 1.0])
        ax1.set_ylim([0.0, 1.05])
        ax1.set_xlabel('False Positive Rate (FPR)', fontweight='bold')
        ax1.set_ylabel('True Positive Rate (TPR)', fontweight='bold')
        ax1.set_title('Receiver Operating Characteristic (ROC)', fontsize=11, fontweight='bold')
        ax1.legend(loc="lower right")
        ax1.grid(True, linestyle='--', alpha=0.5)

        disp = ConfusionMatrixDisplay(confusion_matrix=cm, display_labels=['Benign', 'Ransomware'])
        disp.plot(ax=ax2, cmap='Blues', values_format='d')
        ax2.set_title('Confusion Matrix', fontsize=11, fontweight='bold')

        plt.tight_layout()
        save_path = self.output_dir / save_name
        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        plt.close()
        print(f"[+] Saved ROC & Confusion Matrix to {save_path}")

    def _generate_svg_doughnut(self, safari_data: Dict[str, Any], save_name: str):
        """Native SVG fallback renderer when Matplotlib is not installed."""
        meta = safari_data.get("metadata", {})
        p = meta.get("pristine_count", 0)
        l = meta.get("lost_count", 0)
        r = meta.get("replica_count", 0)
        total = p + l + r or 1

        svg_content = f"""<svg width="400" height="300" xmlns="http://www.w3.org/2000/svg">
  <rect width="100%" height="100%" fill="#ffffff"/>
  <text x="200" y="30" font-family="sans-serif" font-size="16" font-weight="bold" text-anchor="middle">SAFARI State Summary</text>
  <circle cx="150" cy="160" r="80" fill="#3498db" />
  <circle cx="150" cy="160" r="50" fill="#ffffff" />
  <text x="150" y="155" font-family="sans-serif" font-size="12" text-anchor="middle">Pristine: {p}</text>
  <text x="150" y="175" font-family="sans-serif" font-size="12" text-anchor="middle">Lost: {l}</text>
</svg>"""
        save_path = self.output_dir / save_name
        with open(save_path, "w") as f:
            f.write(svg_content)
        print(f"[+] Saved SVG Doughnut fallback to {save_path}")
