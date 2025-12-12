# Make PSE IO Examples

%.o: %.c pse.h heci_types.h
	cc -c $<

version: pse.o version.o
	cc -o version version.o pse.o

dio: pse.o dio.o
	cc -o dio dio.o pse.o

led: pse.o led.o
	cc -o led led.o pse.o

can: pse.o can.o cansend.o candump.o
	cc -o can can.o pse.o
	cc -o cansend cansend.o pse.o
	cc -o candump candump.o pse.o

automotive: pse.o automotive.o
	cc -o automotive automotive.o pse.o

pwm: pse.o pwm.o
	cc -o pwm pwm.o pse.o

all:
	${MAKE} version
	${MAKE} dio
	${MAKE} led
	${MAKE} can
	${MAKE} automotive
	${MAKE} pwm

clean:
	rm -rf *.o
	rm -rf version dio led can cansend candump automotive pwm
