#!/bin/bash

SRV=localhost
TOPIC=tides/trebeurden/json
DELAY=5
LOOP=1


# Note: ajouter ':' devant les options pour éviter verbose option handling
while getopts "l:d:hi:" opt;
do
	case $opt in
	d)
		DELAY=$OPTARG
		;;
	h)
		echo "usage: $0 [-h] [-d <delay in sec>] [-l <number of loops>] -i file.txt" >&2
		exit 1
		;;
	l)
		LOOP=$OPTARG
		;;
	i)
		FILE=$OPTARG
		;;
	\?)
		echo "Invalid option: -$OPTARG" >&2
		exit 1
		;;
	:)
		echo "Option -$OPTARG requires an argument." >&2
		exit 1
		;;
	esac
done

if [ -z "$FILE" ]
then
	echo "option -i is mandatory"
	exit 1
fi

## exit the script with msg number
function sortir() {
    echo "msg send: $NB"
    exit 0
}

# tide sortir() to SIGINT
trap sortir INT

# get here data beginning
# END_LINE=$( awk '/^__END__/{print NR + 1}' $0)

NB=0 # send msgs
while [ $LOOP -gt 0 ]
do
	# send msgs from here date
	while read l
	do
		mosquitto_pub -h $SRV -t $TOPIC -m "$l"
		NB=$(( $NB + 1 ))
		sleep $DELAY
   #  done < <( tail -n +$END_LINE $0 )
	done < $FILE
	LOOP=$(( $LOOP - 1 ))
	echo -n "."
done
sortir

exit 0
__END__
{"location":"trebeurden","high":{"timestamp":"2018-10-10 19:54 +0200","level":9.87,"coefficient":105},"current":{"timestamp":"10/10/18 19:55 +0200","level":9.87,"clock":0},"low":{"timestamp":"2018-10-11 02:21 +0200","level":0.89,"coefficient":105}}
{"location":"trebeurden","high":{"timestamp":"2018-10-10 19:54 +0200","level":9.87,"coefficient":105},"current":{"timestamp":"10/10/18 20:00 +0200","level":9.87,"clock":2},"low":{"timestamp":"2018-10-11 02:21 +0200","level":0.89,"coefficient":105}}
{"location":"trebeurden","high":{"timestamp":"2018-10-10 19:54 +0200","level":9.87,"coefficient":105},"current":{"timestamp":"10/10/18 20:05 +0200","level":9.86,"clock":4},"low":{"timestamp":"2018-10-11 02:21 +0200","level":0.89,"coefficient":105}}