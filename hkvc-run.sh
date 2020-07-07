#!/bin/bash
# Helper commands
# v20200628IST1708, HanishKVC
#

function math() {
	arg="$2"
	if [ "$1" == "-i" ]; then
		mode="int"
	elif [ "$1" == "-f" ]; then
		mode="float"
	else
		mode="int"
		arg="$1"
	fi
	python3 -c "print($mode($arg),end='')"
}

function _build_prepare() {
	apt-get install build-essential nasm pkg-config libdrm-dev libx265-dev libopus-dev libx264-dev libxcb1-dev libx11-dev libsdl2-dev
}

function _build_configure() {
	./configure --enable-gpl --enable-libx264 --enable-libx265 --enable-libopus --enable-libdrm
}

function _time_ffmpeg() {
	echo -e "\n**** $1 ****\n"
	totalTime=0
	totalTSC=0
	for i in 1 2 3 4; do
		#rm test.mp4; time ./ffmpeg -i ../teststreams/testintelxtile.mp4 $1 test.mp4 2> /tmp/hkvc-run.log
		bash -c "time ./ffmpeg -y -i ../teststreams/testintelxtile.mp4 $1 -f h264 /dev/null" 2> /tmp/hkvc-run.log
		fM=`grep "real" /tmp/hkvc-run.log | cut -f 2 | cut -d 'm' -f 1`
		fS=`grep "real" /tmp/hkvc-run.log | cut -f 2 | cut -d 'm' -f 2 | cut -d 's' -f 1`
		if [ "$DEBUG" == "1" ]; then
			grep "real" /tmp/hkvc-run.log
		fi
		curTime=`math "$fM*60*100+$fS*100"`
		totalTime=$(($totalTime+$curTime))
		if [ "$DEBUG" == "1" ]; then
			grep "perf" /tmp/hkvc-run.log
		fi
		curTSC=`grep "perf" /tmp/hkvc-run.log | cut -d ' ' -f 6`
		if [ "$curTSC" != "" ]; then
			totalTSC=$(($totalTSC+$curTSC))
		fi
		echo -n -e "curReal: $fM:$fS, curTSC: $curTSC\r"
	done
	AvgTime=`math -f "$totalTime/(4*100)"`
	AvgTSC=`math -f "$totalTSC/4"`
	echo "AvgTime=$AvgTime, AvgTSC=$AvgTSC"
}

function time_fbdetile() {
	_time_ffmpeg "-vf fbdetile=1"
	_time_ffmpeg
	_time_ffmpeg "-vf fbdetile=0"
	_time_ffmpeg "-vf fbdetile=1"
	_time_ffmpeg "-vf fbdetile=2"
	_time_ffmpeg "-vf fbdetile=3"
}

function test_fbdetile() {
	hkvc/hkvc-tile-image.py
	for i in 0 1 2 3 4; do
		rm /tmp/t.png; ./ffmpeg -i /tmp/ssti.png -vf fbdetile=$i /tmp/t.png; xdg-open /tmp/t.png
		read -p "that was fbdetile=$i"
	done
}

$@

