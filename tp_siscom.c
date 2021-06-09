/**
 * @file tp_siscom.c
 * @author Juan Ignacio Fernandez (juanfernandez@mi.unc.edu.ar) y Juan Pablo
 * Saucedo
 * @brief Manejador de dispositivo de caracter que desencripta los caracteres
 * ingresados.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

int init_module (void);
void cleanup_module (void);
static int device_open (__attribute__ ((unused)) struct inode *inode,
                        __attribute__ ((unused)) struct file *file);
static int device_release (__attribute__ ((unused)) struct inode *inode,
                           __attribute__ ((unused)) struct file *file);
static ssize_t device_read (__attribute__ ((unused)) struct file *filp,
                            char *buffer, size_t length,
                            __attribute__ ((unused)) loff_t *offset);
static ssize_t device_write (__attribute__ ((unused)) struct file *filp,
                             const char *buff, size_t len,
                             __attribute__ ((unused)) loff_t *off);
static int mychardev_uevent (__attribute__ ((unused)) struct device *dev,
                             struct kobj_uevent_env *env);
static int get_inputs (void *data);

/* Task handle to identify thread */
static struct task_struct *ts = NULL;

/* Define BUTTONs */
static struct gpio buttons[] = {
{ 12, GPIOF_IN, "BUTTON 1" },
{ 13, GPIOF_IN, "BUTTON 2" },
{ 19, GPIOF_IN, "BUTTON 3" },
{ 26, GPIOF_IN, "BUTTON 4" },
};

#define SUCCESS 0
#define DEVICE_NAME "siscom" /*!< Nombre del dispositivo (/proc/devices). */
#define BUF_LEN 128          /*!< Longitud maxima del mensaje. */

MODULE_LICENSE ("Dual BSD/GPL");
MODULE_AUTHOR ("FERNANDEZ, Juan Ignacio");

static int major;           /*!< Numero major asignado al dispositivo. */
static int Device_Open = 0; /*!< 0 -> No abierto ; 1 -> Abierto.  Se usa para
                                     impedir multiples disp abiertos. */
static struct class *myclass = NULL; /*!< Clase */
static char *valor;
static char *mensaje;

/*
 * Esta estructura ejecutará las funciones que se llamen cuando
 * un proceso hace algo al dispositivo.
 *
 */
static struct file_operations fops = { .read = device_read,
                                       .write = device_write,
                                       .open = device_open,
                                       .release = device_release };

/**
 * @brief Esta función se llama cuando se carga el módulo.
 *
 * @return int 0 en el caso que finalice correctamente.
 */
int
init_module (void)
{
  int ret = 0;
  if ((valor = kmalloc (BUF_LEN, GFP_KERNEL)) == 0)
    {
      printk (KERN_ALERT "SisCom: Fallo al crear variable valor:\n");
      return -1;
    }

  if ((mensaje = kmalloc (BUF_LEN, GFP_KERNEL)) == 0)
    {
      printk (KERN_ALERT "SisCom: Fallo al crear variable mensaje:\n");
      kfree (valor);
      return -1;
    }

  memset (valor, ' ', BUF_LEN);
  memset (mensaje, ' ', BUF_LEN);

  /* Se registra el dispositivo de caracter */
  major = register_chrdev (0, DEVICE_NAME, &fops);
  if (major < 0)
    {
      printk (KERN_ALERT
              "SisCom: Fallo en registrar el dispositivo caracter: %d.\n",
              major);
      kfree (mensaje);
      kfree (valor);
      return major;
    }
  /* Se crea la clase */
  myclass = class_create (THIS_MODULE, DEVICE_NAME);
  if (IS_ERR (myclass))
    {
      pr_err (KERN_ERR "SisCom: Error al crear la case %s.\n", DEVICE_NAME);
      kfree (mensaje);
      kfree (valor);
      unregister_chrdev ((unsigned int)major, DEVICE_NAME);
      return (int)(PTR_ERR (myclass));
    }
  myclass->dev_uevent = mychardev_uevent;

  /* Se crea el dispositivo */
  if (IS_ERR (
          device_create (myclass, NULL, MKDEV (major, 0), NULL, DEVICE_NAME)))
    {
      pr_err (KERN_ERR "SisCom: Error al creal el dispositivo %s.\n",
              DEVICE_NAME);
      kfree (mensaje);
      kfree (valor);
      class_destroy (myclass);
      unregister_chrdev ((unsigned int)major, DEVICE_NAME);
      return -1;
    }

    // register BUTTON gpios
  ret = gpio_request_array (buttons, ARRAY_SIZE (buttons));

  if (ret)
    {
      printk (KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
      kfree (mensaje);
      kfree (valor);
      device_destroy (myclass, MKDEV (major, 0));
      class_destroy (myclass);
      unregister_chrdev ((unsigned int)major, DEVICE_NAME);
      return ret;
    }

  ts = kthread_create (get_inputs, NULL, "get_inputs");

  if (ts)
    {
      wake_up_process (ts);
    }
  else
    {
      printk (KERN_ERR "Unable to create thread\n");
      kfree (mensaje);
      kfree (valor);
      device_destroy (myclass, MKDEV (major, 0));
      class_destroy (myclass);
      unregister_chrdev ((unsigned int)major, DEVICE_NAME);
      gpio_free_array (buttons, ARRAY_SIZE (buttons));
      return -1;
    }

  printk (KERN_INFO
          "SisCom: Inicializado con exito. Nombre: %s , Major: %d.\n",
          DEVICE_NAME, major);
  printk (KERN_INFO "SisCom: Dispositivo: /dev/%s .\n", DEVICE_NAME);

  return SUCCESS;
}

/**
 * @brief Esta función se llama cuando se descarga/elimina el módulo.
 *
 */
void
cleanup_module (void)
{

  // gpio_free_array (buttons, ARRAY_SIZE (buttons));

  /* Se elimina/desregistra el dispositivo y la clase */
  device_destroy (myclass, MKDEV (major, 0));
  printk (KERN_INFO "SisCom: Se elimino el dispositivo\n");
  unregister_chrdev ((unsigned int)major, DEVICE_NAME);
  printk (KERN_INFO "SisCom: Se quito el registro del dispositivo\n");
  class_destroy (myclass);
  printk (KERN_INFO "SisCom: Se elimino la clase\n");

  // Se libera valor y mesnaje
  //  __clear_user(valor, BUF_LEN);
  kfree (valor);
  printk (KERN_INFO "SisCom: Se libro valor\n");
  //  __clear_user(mensaje, BUF_LEN);
  kfree (mensaje);
  printk (KERN_INFO "SisCom: Se libro mensaje\n");

  gpio_free_array (buttons, ARRAY_SIZE (buttons));
  printk (KERN_INFO "SisCom: Se liberaron los botones\n");

  // Se termina el hilo
  if (ts)
    {
      kthread_stop (ts);
      printk (KERN_INFO "SisCom: Se libro hilo\n");
    }
}

/**
 * @brief La función de apertura del dispositivo que se llama cada vez que se
 * abre el dispositivo.
 *
 * @param inode Puntero a un objeto de inodo (definido en linux/fs.h).
 * @param file Puntero a un objeto de archivo (definido en linux/fs.h).
 * @return int 0 en el caso que finalice correctamente.
 */
static int
device_open (__attribute__ ((unused)) struct inode *inode,
             __attribute__ ((unused)) struct file *file)
{
  if (Device_Open)
    return -EBUSY;
  Device_Open++;
  try_module_get (THIS_MODULE);
  return SUCCESS;
}

/**
 * @brief La función de cierre del dispositivo que se llama cada vez que se
 * cierra el dispositivo.
 *
 * @param inode Puntero a un objeto de inodo (definido en linux/fs.h).
 * @param file Puntero a un objeto de archivo (definido en linux/fs.h).
 * @return int 0 en el caso que finalice correctamente.
 */
static int
device_release (__attribute__ ((unused)) struct inode *inode,
                __attribute__ ((unused)) struct file *file)
{
  Device_Open--;
  module_put (THIS_MODULE);
  return 0;
}

/**
 * @brief Esta función se llama cuando un proceso, que ya abrió el archivo dev,
 * intenta leerlo.
 *
 * @param filp Un puntero a un objeto de archivo (definido en linux/fs.h).
 * @param buffer Puntero al búfer para escribe los datos.
 * @param length Longitud del bufer.
 * @param offset Offset del buffer.
 * @return ssize_t cantidad de bytes escritos en el buffer.
 */
static ssize_t
device_read (__attribute__ ((unused)) struct file *filp, char *buffer,
             size_t length, __attribute__ ((unused)) loff_t *offset)
{
  int bytes_read = 0;

  if (*valor == 0)
    return 0;

  /* Carga datos en buffer. */
  while (length && *valor)
    {
      put_user (*(valor++), buffer++);
      length--;
      bytes_read++;
    }

  return bytes_read;
}

/**
 * @brief Esta función se llama siempre que se escribe en el dispositivo desde
 * el espacio del usuario (echo "hi" > /dev/SisCom). Ademas de recibr la
 * cadena llama a la funcion para desencriptar.
 *
 * @param filp Puntero a un objeto de archivo.
 * @param buff Búfer que contiene la cadena para escribir en el dispositivo.
 * @param len Longitud del buffer.
 * @param off Offset del buffer.
 * @return ssize_t Cantidad de datos escritos/cifrados.
 */
static ssize_t
device_write (__attribute__ ((unused)) struct file *filp, const char *buff,
              size_t len, __attribute__ ((unused)) loff_t *off)
{
  if (len > BUF_LEN)
    {
      len = BUF_LEN;
      printk (KERN_WARNING "SisCom: Se ha recortado el mensaje.\n");
    }
  printk (KERN_INFO "SisCom: Iniciando copia del mensaje.\n");
  if (copy_from_user (mensaje, buff, len))
    {
      printk (KERN_ERR "SisCom: Error al copiar el mensaje.\n");
      return 0;
    }
  if (__clear_user (mensaje, BUF_LEN))
    {
      printk (KERN_ERR "SisCom: Error al limpiar el mensaje.\n");
      return 0;
    }
  printk (KERN_INFO "SisCom: Iniciando desencriptacion.\n");
  // decrypt_string (len);
  return len;
}

/**
 * @brief Cambia el permiso del dispositivo a 0666.
 *
 * @param dev Puntero al dispositivo.
 * @param env Puntero a env.
 * @return int 0 en el caso que finalice correctamente.
 */
static int
mychardev_uevent (__attribute__ ((unused)) struct device *dev,
                  struct kobj_uevent_env *env)
{
  add_uevent_var (env, "DEVMODE=%#o", 0666);
  return 0;
}

static int
get_inputs (void *data)
{
  int valint;
  printk (KERN_INFO "SisCom:Entre.\n");

  // loop until killed ...
  while (!kthread_should_stop ())
    {
      valint = 256;

      memset (valor, ' ', BUF_LEN);
      sprintf (valor, "%d ", valint);

      printk (KERN_INFO "SisCom: Hola\n");
      mdelay (5000);
    }

  return 0;
}