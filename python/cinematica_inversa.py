import numpy as np
import math
"""
x,y,z=10,7,8
medidas_eslabones=np.array([10, 15, 20], dtype=np.float32)

theta = np.arctan2(y, x)"""



#PARA UNA SOLUCION CODO ARRIBA CAMBIAN LOS VALORES DE THETA1 Y THETA2
x = 50    # Posición de la herramienta en x
y = 50   # Posición de la herramienta en y
z = 50    # Posición de la herramienta en z
gamma = 0 # ángulo de elevación de la herramienta, gamma en grados

gamma = gamma*math.pi/180  # Las funciones trigonometricas de math trabajan 
                           # con los angulos en radianes
# Parámetros del robot
d1 = 50  #medidas en 
a2 = 40
a3 = 40
d5 = 20

theta1 = math.atan2(y,x)
print("th1 =",theta1*180/math.pi)

xy = math.sqrt(x*x + y*y)

e = z - d1 - d5*math.sin(gamma)

f = xy - d5*math.cos(gamma)

beta = math.atan2(e,f)
 
c = math.sqrt(e**2 + f**2) 

varfi = math.acos((a3**2 - c**2 - a2**2)/(-2*c*a2))

eta = math.acos((c**2 - a3**2 - a2**2)/(-2*a3*a2))

theta2 = beta + varfi
print("th2 =",theta2*180/math.pi)

theta3 =  eta - math.pi
print("th3 =",theta3*180/math.pi)

theta4 = gamma + math.pi/2 - theta2 - theta3
print("th4 =",theta4*180/math.pi)

th1 = theta1*180/math.pi
th2 = theta2*180/math.pi
th3 = theta3*180/math.pi
th4 = theta4*180/math.pi

print("Beta = ",beta*180/math.pi)
print("Varfi = ",varfi*180/math.pi)
print("Eta = ",eta*180/math.pi)




