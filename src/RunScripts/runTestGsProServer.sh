# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh

#rm -f Logs/*.log

$PITRAC_ROOT/src/build/pitrac_lm  --system_mode test_gspro_server  --search_center_x 700 --search_center_y 650 --logging_level=trace --artifact_save_level=final_results_only

# --post-process-file /mnt/VerdantShare/dev/GolfSim/LM/src/assets/motion_detect.json
