# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh

#rm -f Logs/*.log

sudo -E nice -n -20 $PITRAC_ROOT/src/build/pitrac_lm  --run_single_pi --system_mode test $PITRAC_COMMON_CMD_LINE_ARGS  --search_center_x 850 --search_center_y 500 --logging_level=trace --artifact_save_level=final_results_only
