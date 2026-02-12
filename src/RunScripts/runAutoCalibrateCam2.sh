# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh

#rm -f Logs/*.log

$PITRAC_ROOT/src/build/pitrac_lm  --system_mode camera2AutoCalibrate  $PITRAC_COMMON_CMD_LINE_ARGS  --search_center_x 720 --search_center_y 500 --logging_level=trace --artifact_save_level=final_results_only
