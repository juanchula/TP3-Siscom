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
#include <linux/string.h>
#include <linux/uaccess.h>

/* Task handle to identify thread */
static struct task_struct *ts = NULL;

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

#define SUCCESS 0
#define DEVICE_NAME "siscom" /*!< Nombre del dispositivo (/proc/devices). */
#define BUF_LEN 10           /*!< Longitud maxima del mensaje. */

MODULE_LICENSE ("Dual BSD/GPL");
MODULE_AUTHOR ("FERNANDEZ, Juan Ignacio");

static int major;           /*!< Numero major asignado al dispositivo. */
static int Device_Open = 0; /*!< 0 -> No abierto ; 1 -> Abierto.  Se usa para
                                     impedir multiples disp abiertos. */
static struct class *myclass = NULL; /*!< Clase */
static char msg[BUF_LEN] = { 0 };    /*!< Mensaje a mostrar en dev. */
static char *msg_Ptr;                /*!< Puntero al mensaje. */

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
  /* Se registra el dispositivo de caracter */
  major = register_chrdev (0, DEVICE_NAME, &fops);
  if (major < 0)
    {
      printk (KERN_ALERT
              "SisCom: Fallo en registrar el dispositivo caracter: %d.\n",
              major);
      return major;
    }
  /* Se crea la clase */
  myclass = class_create (THIS_MODULE, DEVICE_NAME);
  if (IS_ERR (myclass))
    {
      pr_err (KERN_ERR "SisCom: Error al crear la case %s.\n", DEVICE_NAME);
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
      class_destroy (myclass);
      unregister_chrdev ((unsigned int)major, DEVICE_NAME);
      return -1;
    }

  printk (KERN_INFO
          "SisCom: Inicializado con exito. Nombre: %s , Major: %d.\n",
          DEVICE_NAME, major);
  printk (KERN_INFO "SisCom: Dispositivo: /dev/%s .\n", DEVICE_NAME);

  ts = kthread_create (get_inputs, NULL, "get_inputs");

  if (ts)
    {
      wake_up_process (ts);
    }

  return SUCCESS;
}

/**
 * @brief Esta función se llama cuando se descarga/elimina el módulo.
 *
 */
void
cleanup_module (void)
{
  /* Se elimina/desregistra el dispositivo y la clase */
  unregister_chrdev ((unsigned int)major, DEVICE_NAME);
  device_destroy (myclass, MKDEV (major, 0));
  class_destroy (myclass);
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
  msg_Ptr = msg;
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

  if (*msg_Ptr == 0)
    return 0;

  /* Carga datos en buffer. */
  while (length && *msg_Ptr)
    {
      put_user (*(msg_Ptr++), buffer++);
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
  //   if (len > BUF_LEN)
  //     {
  //       len = BUF_LEN;
  //       printk (KERN_WARNING "SisCom: Se ha recortado el mensaje.\n");
  //     }
  //   printk (KERN_INFO "SisCom: Iniciando copia del mensaje.\n");
  //   if (copy_from_user (msg, buff, len))
  //     {
  //       printk (KERN_ERR "SisCom: Error al copiar el mensaje.\n");
  //       return 0;
  //     }
  //   if (__clear_user (msg, BUF_LEN))
  //     {
  //       printk (KERN_ERR "SisCom: Error al limpiar el mensaje.\n");
  //       return 0;
  //     }
  //   printk (KERN_INFO "SisCom: Iniciando desencriptacion.\n");
  //   decrypt_string (len);
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
  int i = 1;
  int j, k, icopy;
  char val[5], valinv[5];
  printk (KERN_INFO "SisCom:Entre.\n");

  // loop until killed ...
  while (!kthread_should_stop ())
    {
      if (i > 1000)
        i = 0;
      // msg[0] = (char) i;
      // msg[1] = '/0';

      // if (copy_from_user (msg, buff, len))
      // {
      //   printk (KERN_ERR "SisCom: Error al copiar el mensaje.\n");
      //   return 0;
      // }

      // val[0] = '0';
      j = 0;
      icopy = i;
      while (icopy)
        {
          valinv[j++] = icopy % 10 + '0';
          icopy /= 10;
        }
      for (k = 0; k < j; k++)
        {
          val[k] = valinv[j - k - 1];
        }

      // val[j] = '/0';
      // memcpy(msg, i, sizeof(i));
      // sprintf

      // strcpy(msg, i);
      strcpy (msg, val);
      // i++;
      printk (KERN_INFO "SisCom: Nuevo mensaje: %s. val: %s valor i: %d\n",
              msg, val, i);
      mdelay (1000);
      i++;
    }

  return 0;
}