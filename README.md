# X4C-Moscatel

Este es un GEM de gemini que contiene la información acerca de nuestro proyecto. Recomiendo hacer las consultas acerca de nuestro proyecto con el.

https://gemini.google.com/gem/1v8jnuFOj4cJeROFavsBkOiZ2vt1CeKzN?usp=sharing

He hecho un diseño de una PCB que (creo) que contiene toda la funcionalidad que necesitamos pero no se si daría tiempo a encargarla/entra dentro del presupuesto(me mola diseñar PCBs y estoy practicando xd). 

Por otro lado, aqui os dejo una descripción de lo que consiste el proyecto por si alguno aún tiene ciertas dudas:

El proyecto consiste en un nodo **IoT de borde (Edge Computing)** diseñado para la monitorización de la infraestructura ferroviaria. Su función es capturar la "firma vibratoria" y la posición geográfica del tren en tiempo real para crear un modelo digital (Gemelo Digital) que permita predecir fallos en la vía o en el material rodante.

**El Flujo de Datos:**
  - Captura: El ICM-20948 (IMU de 9 ejes) detecta aceleraciones y rotaciones.

  - Geolocalización: El NEO-6M vincula cada evento de vibración a una coordenada GPS exacta.

  - Procesamiento: El ESP32-S3 filtra los datos y gestiona las colas de escritura.

  - Persistencia: Almacenamiento masivo en MicroSD mediante bus SPI.

**Los 3 Pilares del Gemelo Digital**

  - El Objeto Físico: El tren y la infraestructura (vías, catenarias).

  - El Modelo Virtual: Una representación matemática o de simulación (CAD, modelos de fatiga de materiales, algoritmos de predicción).

  - La Conexión de Datos (Aquí entramos nosotros): El flujo constante de información desde los sensores (nuestro ESP32-S3) hacia el modelo virtual.

Desde la perspectiva de firmware en el ESP32-S3, el **algoritmo funcionaría** como un sistema de fusión de datos en tiempo real **siguiendo estos pasos:**

  **1. Adquisición y Sincronización (Multi-threading):**
       
  El ESP32-S3 tiene dos núcleos. Aprovecharemos esto para que el algoritmo no pierda datos
     
  - Núcleo 0 (Sensor Core): Muestrea la IMU (ICM-20948) a alta frecuencia (ej. 200Hz - 500Hz). Las vibraciones ferroviarias son de alta frecuencia y si muesteramos lento, perdemos el "aliasing" de la avería.
       
  - Núcleo 1 (Sync & Storage): Lee el GPS (NEO-6M) a 1Hz o 5Hz y gestiona la escritura en la MicroSD.
       
  **2. El "Latido" del Algoritmo (Data Fusion)**
   
   El algoritmo crea un paquete de datos (struct) que une ambos mundos. No basta con guardar la vibración; cada ráfaga de aceleración se vincula al último dato GPS conocido:  

  struct RegistroTelemetria {
     
  uint32_t timestamp;  // Tiempo interno (ms)
      
  float accX, accY, accZ; // Datos IMU
      
  double lat, lon;     // Ubicación GPS
      
  float velocidad;     // Velocidad en km/h calculada por el NEO-6M
      
  };

  **3. Lógica de Filtrado y Disparo (Event-Based):**
  
   Para no llenar la MicroSD con gigabytes de "ruido" cuando el tren está parado, el algoritmo aplica filtros:
   - Umbral de Movimiento: Si la velocidad del GPS es < 2 km/h y la IMU no detecta cambios, el sistema entra en modo ahorro o baja la tasa de muestreo.
   - Detección de Impactos (G-Force): Si la aceleración en el eje Z (vertical) supera un umbral preestablecido (ej. 1.5g), el algoritmo marca ese registro como "EVENTO CRÍTICO" para que el Gemelo Digital le dé prioridad en el análisis.

  **4. Compensación de Velocidad:**
     
  Aquí está la clave del Gemelo Digital: el algoritmo debe normalizar los datos. Calcula la RMS (Root Mean Square) de la vibración en ventanas de tiempo y la divide por la velocidad actual. Si el resultado aumenta con el tiempo a la misma velocidad, el Gemelo Digital detecta una degradación de la vía.
  
  **5. Almacenamiento Seguro (Safe Write):**
     
  Debido a las vibraciones, la escritura en SD es crítica. El algoritmo no escribe cada dato individualmente (destruiría la latencia), sino que llena un buffer en la RAM del ESP32-S3 y realiza "escrituras en bloque" cada 1 o 2 segundos, cerrando y abriendo el archivo periódicamente para evitar la corrupción de datos si hay un corte de energía.

Esto es todo, salu2(si alguno ha llegado hasta aqui xd).
