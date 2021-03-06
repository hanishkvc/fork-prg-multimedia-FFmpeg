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

function time_fbtiler_detile() {
	_time_ffmpeg "-vf fbtiler=op=2:layout=1"
	_time_ffmpeg
	_time_ffmpeg "-vf fbtiler=op=2:layout=0"
	_time_ffmpeg "-vf fbtiler=op=2:layout=1"
	_time_ffmpeg "-vf fbtiler=op=2:layout=2"
	_time_ffmpeg "-vf fbtiler=op=2:layout=3"
}

function time_fbtiler_tile() {
	_time_ffmpeg "-vf fbtiler=op=1:layout=1"
	_time_ffmpeg
	_time_ffmpeg "-vf fbtiler=op=1:layout=0"
	_time_ffmpeg "-vf fbtiler=op=1:layout=1"
	_time_ffmpeg "-vf fbtiler=op=1:layout=2"
	_time_ffmpeg "-vf fbtiler=op=1:layout=3"
}

function test_fbtiler_detile() {
	hkvc/hkvc-tile-image.py
	for i in 0 1 2 3 4; do
		op="-vf fbtiler=op=2:$i"
		rm /tmp/t.png; ./ffmpeg -i /tmp/ssti.png $op /tmp/t.png; xdg-open /tmp/t.png
		read -p "that was $op"
	done
}

function _test_fbtiler() {
	op="$1"
	in="/tmp/$2"
	out="/tmp/$3"
	rm $out; ./ffmpeg -i $in $op $out; xdg-open $out
	read -p "Just finished $op"
}

function test_fbtiler() {
	rm -i /tmp/ssti.png
	if [ -e /tmp/ssti.png ]; then
		read -p "Using existing ssti.png"
	else
		read -p "Will use a new ssti.png"
		hkvc/hkvc-tile-image.py
	fi
	_test_fbtiler "-vf fbtiler=op=0:layout=0" ssti.png t.png
	_test_fbtiler "-vf fbtiler=op=1:layout=0" ssti.png t.png
	_test_fbtiler "-vf fbtiler=op=2:layout=0" ssti.png t.png
	_test_fbtiler "-vf fbtiler=op=1:layout=1" ssti.png t_tx.png
	_test_fbtiler "-vf fbtiler=op=2:layout=1" t_tx.png t_dx.png
	_test_fbtiler "-vf fbtiler=op=1:layout=2" ssti.png t_ty.png
	_test_fbtiler "-vf fbtiler=op=2:layout=2" t_ty.png t_dy.png
	_test_fbtiler "-vf fbtiler=op=1:layout=3" ssti.png t_tyf.png
	_test_fbtiler "-vf fbtiler=op=2:layout=3" t_tyf.png t_dyf.png
}

function test_fbtiler_hw() {

	sudo rm /tmp/t.mp4; sudo ./ffmpeg -f kmsgrab -i - -vf hwdownload,format=bgr0 /tmp/t.mp4
	read -p "Capture with hwcontext_drm conv, this should play proper"
	./ffplay -i /tmp/t.mp4

	sudo rm /tmp/t.mp4; sudo ./ffmpeg -f kmsgrab -format_modifier 2 -i - -vf hwdownload,format=bgr0 /tmp/t.mp4
	read -p "Capture with hwcontext_drm conv wrong format_modifier, this should not play proper"
	./ffplay -i /tmp/t.mp4
	read -p "this should play proper"
	./ffplay -i /tmp/t.mp4 -vf fbtiler=op=2:layout=1

	sudo rm /tmp/t.mp4; sudo ./ffmpeg -f kmsgrab -format_modifier 2 -i - -vf hwdownload=0,format=bgr0 /tmp/t.mp4
	read -p "Capture with hwcontext_drm conv wrong format_modifier, hwdownload passthrough, this should not play proper"
	./ffplay -i /tmp/t.mp4
	read -p "this should play proper"
	./ffplay -i /tmp/t.mp4 -vf fbtiler=op=2:layout=1

	sudo rm /tmp/t.mp4; sudo ./ffmpeg -f kmsgrab -format_modifier 2 -i - -vf hwdownload=1,format=bgr0 /tmp/t.mp4
	read -p "Capture with hwcontext_drm conv wrong format_modifier, hwdownload conv, this should play proper"
	./ffplay -i /tmp/t.mp4

}

$@

