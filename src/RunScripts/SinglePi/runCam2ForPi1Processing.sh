# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh


#rm -f Logs/*.log

$PITRAC_ROOT/src/build/pitrac_lm  --run_single_pi  --system_mode runCam2ProcessForPi1Processing $PITRAC_COMMON_CMD_LINE_ARGS  --camera_gain 3.6 --logging_level=info --artifact_save_level=final_results_only

