from threading import Thread
import os
import time

def readDevice(loopCount, delay):
    print("Reading from Device")
    for x in range(loopCount):
        device = open("/dev/pa2_out", "r")
        fileContents = device.read()
        device.close()
        time.sleep(delay/1000)

def writeDevice(inputString, loopCount, delay):
    print("Writing to Device")
    for x in range(loopCount):
        device = open("/dev/pa2_in", "w")
        device.write(inputString)
        device.close()
        time.sleep(delay/1000)

def main():
    writerThread = Thread(target=writeDevice, args=("Hello", 50, 10))
    readerThread = Thread(target=readDevice, args=(50,10))

    writerThread.start()
    readerThread.start()

    writerThread.join()
    readerThread.join()

    print("Done!")

if __name__ == "__main__":
    main()
