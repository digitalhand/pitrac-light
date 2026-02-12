# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#

#!/bin/bash

. $PITRAC_ROOT/src/RunScripts/runPiTracCommon.sh

#rm -f Logs/*.log

$PITRAC_ROOT/src/build/pitrac_lm  --system_mode test_spin  --wait_keys --show_image --message_host 10.0.0.65  --search_center_x 700 --search_center_y 600

# --post-process-file /mnt/VerdantShare/dev/GolfSim/LM/src/assets/motion_detect.json
