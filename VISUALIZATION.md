# Audio Fingerprint Visualization Guide

This document explains how to visualize audio fingerprinting and matching processes to help identify issues such as false-positive matches.

## Requirements

To generate and view visualizations, you need:

- Python 3.x
- Matplotlib package (`pip install matplotlib`)

## Generating Visualizations

Add the `--visualize` flag to your command when generating fingerprints or matching audio:

```bash
# When generating fingerprints
./AFingerprint generate shazam catalog.db your_audio.pcm --visualize

# When matching audio
./AFingerprint match shazam catalog.db query_audio.pcm --visualize
```

This will create:
1. JSON files with fingerprint data
2. Python scripts to generate plots

## Running Visualizations

After generating the data and scripts, run:

```bash
./run_visualizations.sh
```

This will:
1. Check for required Python packages
2. Run all visualization scripts
3. Generate PNG image files

## Understanding the Visualizations

### Extraction Visualization
Shows how fingerprints are extracted from audio:
- **Blue dots**: All detected peaks in the audio spectrum
- **Red dots**: Selected fingerprint points

### Matching Visualization
Compares source (database) and query audio:
- **Top plot**: Source audio fingerprints 
- **Bottom plot**: Query audio fingerprints
- **Orange stars**: Matched fingerprint points

## Identifying False Positives

False positive matches can be identified by looking at the matching visualization:

1. Look for patterns in the matched points:
   - Random scattered matches across the time-frequency domain likely indicate false positives
   - Clustered matches with consistent time offsets indicate true matches

2. Check the number of matched points:
   - A small number of matches (like 20) spread across different time-frequency regions indicates a false positive
   - True matches typically show many points clustered along a time-aligned path

3. Examine the time offsets:
   - True matches show consistent time offsets between source and query fingerprints
   - False positives show random, inconsistent offsets

## Adjusting System Parameters

If you identify false positives, consider:

1. Increasing the `minMatchesRequired_` parameter to require more matches
2. Adjusting the `offsetTolerance_` to be more strict
3. Modifying the peak selection or hash generation algorithm to create more unique fingerprints

By visualizing the matching process, you can gain insights into how the fingerprinting system is performing and make targeted improvements to reduce false positives. 