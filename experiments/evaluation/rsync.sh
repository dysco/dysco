# This file has to bee updated with the names of the machines
# and the coorect directories

if [ $# -eq 1 ]
   then
	rsync -v -tr $1 dx001:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx003:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx004:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx005:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx006:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx007:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx008:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx009:eval_dysco/dysco_sigcomm17/evaluation
	rsync -v -tr $1 dx010:eval_dysco/dysco_sigcomm17/evaluation
else
	echo Usage: rsync.sh "<directory>"
fi

