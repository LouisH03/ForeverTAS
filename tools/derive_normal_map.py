#!/usr/bin/env python3
"""Derive a tile-safe OpenGL tangent-space normal map from an albedo image."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--strength", type=float, default=2.0)
    parser.add_argument("--blur", type=float, default=1.0)
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    height_image = source.convert("L")
    if args.blur > 0.0:
        height_image = height_image.filter(
            ImageFilter.GaussianBlur(radius=args.blur)
        )
    height = np.asarray(height_image, dtype=np.float32) / 255.0

    # Periodic central differences keep the generated map continuous at tile
    # boundaries. OpenGL normals use positive green for increasing texture V.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    normal = np.dstack(
        (-dx * args.strength, dy * args.strength, np.ones_like(height))
    )
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    encoded = np.clip((normal * 0.5 + 0.5) * 255.0, 0.0, 255.0).astype(
        np.uint8
    )

    args.destination.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(encoded, mode="RGB").save(args.destination, optimize=True)


if __name__ == "__main__":
    main()
