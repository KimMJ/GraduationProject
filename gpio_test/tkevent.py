from Tkinter import *
import RPi.GPIO as GPIO
import time

root = Tk()
root.title("Callback and Event Test")
root.geometry("100x100+300+300")

GPIO.setmode(GPIO.BCM)
GPIO.setup(23, GPIO.OUT)
GPIO.setup(24, GPIO.OUT)

def callback():
    print "button clicked"
    print "LED on"
    GPIO.output(23, True)
    GPIO.output(24, True)
    time.sleep(1)
    print "LED off"
    GPIO.output(23, False)
    GPIO.output(24, False)

button = Button(root, text="click me!", width=10, command=callback)
button.pack(padx = 10, pady = 10)
root.mainloop()
