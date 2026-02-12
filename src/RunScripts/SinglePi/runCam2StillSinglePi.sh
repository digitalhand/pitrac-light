# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
#!/bin/bash

. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh


# Resolution can also by 640x480, 1280x800
sudo -E nice -n -10 $PITRAC_ROOT/src/build/pitrac_lm  --run_single_pi --system_mode camera2 $PITRAC_COMMON_CMD_LINE_ARGS --cam_still_mode --output_filename=cam2_still_picture.png  --search_center_x 650 --search_center_y 500 --logging_level=trace
