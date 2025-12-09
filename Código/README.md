# Bibliotecas
Las bibliotecas usadas en estre proyecto son

### Drivers pantalla OLED
<img width="305" height="373" alt="image" src="https://github.com/user-attachments/assets/e963d0ff-eb10-494d-80c3-4a65297995a3" />
<img width="312" height="358" alt="image" src="https://github.com/user-attachments/assets/22eac0f7-0305-42d3-9449-d7f3862068ba" />
<img width="303" height="357" alt="image" src="https://github.com/user-attachments/assets/b85ddd6d-51a1-4cdb-8f85-749085040622" />

### Implementación e interpretación ESP32
<img width="302" height="359" alt="image" src="https://github.com/user-attachments/assets/d1ea7c00-f3e0-4aeb-8e3a-e0410f680b1b" />

### PZEM004T-V3
<img width="301" height="342" alt="image" src="https://github.com/user-attachments/assets/83661667-1030-4fa7-8dd4-f2cca16e5059" />

# Resumen

El codigo se realizo mediante Arduino IDE, este se guarda en el Esp32 para controlar todos los modulos usados y enviar los datos a la api.

Inicialmente, verifica la comunicación con el Pzem en la consola y establece conexión con la red wifi seleccionada, posteriormente inicia en el menú 0 en la pantalla OLED con el estrato 3 como predeterminado, con la carga actual, cada 3 segundos simula 30 minutos y los suma en el costo acumulado y los kWh, al poner o quitar la carga, no se elimina el acumulado.

En el menú 1, se pueden ver los valores actuales medidos por el Pzem (Voltaje, Corriente, Potencia y Factor de potencia) el Pzem calcula la potencia de la siguiente manera *V x I x Pf = W*. 

En el menú 2, se calculan los costos mensuales con la carga actual junto a la acumulación real de los kWh y la acumulación proyectada.

Finalmente, en el menú 3, se puede ver la conexión con la red wifi seleccionada, con el boton de restar se puede desconectar y conectar a esta misma red, tambien se puede ver el tiempo restante para el envio de datos a la api (establecido en 10 segundos).

Cabe aclarar que el estrato seleccionado con el boton de estratos aplica para todos los menús y los costos se ven afectados por el mismo.
