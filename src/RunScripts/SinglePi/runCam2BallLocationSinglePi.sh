# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash


. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh

sudo -E nice -n -10 $PITRAC_ROOT/src/build/pitrac_lm --run_single_pi --system_mode camera2_ball_location  $PITRAC_COMMON_CMD_LINE_ARGS  --search_center_x 723 --search_center_y 544 --logging_level=info --artifact_save_level=all
