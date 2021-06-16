#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h> //kmalloc y demas
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>

/* Prototipo de las funciones */
static int mychardev_uevent (__attribute__ ((unused)) struct device *dev,
                             struct kobj_uevent_env *env);
static int my_open (struct inode *i, struct file *f);
static int my_close (struct inode *i, struct file *f);
static ssize_t my_read (struct file *f, char __user *buf, size_t len,
                        loff_t *off);
static ssize_t my_write (struct file *f, const char __user *buf, size_t len,
                         loff_t *off);
static int __init drv4_init (void);
static void __exit drv4_exit (void);

/*
 * Esta estructura ejecutará las funciones que se llamen cuando
 * un proceso realiza una accion al dispositivo.
 */
static struct file_operations pugs_fops = { .owner = THIS_MODULE,
                                            .open = my_open,
                                            .release = my_close,
                                            .read = my_read,
                                            .write = my_write };

static dev_t first;       /*!< Primer numero del dispositivo */
static struct cdev c_dev; /*!<Estructura del dispositivo de caracter*/
static struct class *myclass = NULL; /*!< Estructura de la Clase */

/* Define BUTTONs 1*/
static struct gpio buttons1[] = {
  { 10, GPIOF_IN, "BUTTON 1 1" },
  { 11, GPIOF_IN, "BUTTON 1 2" },
};

/* Define BUTTONs 2*/
static struct gpio buttons2[] = {
  { 12, GPIOF_IN, "BUTTON 2 1" },
  { 13, GPIOF_IN, "BUTTON 2 2" },
  { 19, GPIOF_IN, "BUTTON 2 3" },
  { 26, GPIOF_IN, "BUTTON 2 4" },
};

/* Define LEDS */
static struct gpio leds[] = {
  { 16, GPIOF_OUT_INIT_HIGH, "LED 1" },
  { 20, GPIOF_OUT_INIT_HIGH, "LED 2" },
  { 21, GPIOF_OUT_INIT_HIGH, "LED 3" },
};

#define DEVICE_NAME "siscom" /*!< Nombre del dispositivo (/proc/devices). */
#define BUF_LEN 128          /*!< Longitud maxima del mensaje. */
static int selector = 0;     /*!< Sensor seleccionado (0->deshabiltiado). */

static int __init
drv4_init (void)
{
  int ret, i;
  struct device *dev_ret;

  /* Se obtiene de forma dinamica el numero major y minor */
  if ((ret = alloc_chrdev_region (&first, 0, 1, DEVICE_NAME)) < 0)
    {
      printk (KERN_ERR "SisCom: Error obtener major-minor.\n");
      return ret;
    }

  /* Se crea la clase */
  if (IS_ERR (myclass = class_create (THIS_MODULE, DEVICE_NAME)))
    {
      printk (KERN_ERR "SisCom: Error al crear la case %s.\n", DEVICE_NAME);
      unregister_chrdev_region (first, 1);
      return PTR_ERR (myclass);
    }
  /* Se cambia los permisos de la clase */
  myclass->dev_uevent = mychardev_uevent;

  /* Se crea el dispositivo */
  if (IS_ERR (dev_ret
              = device_create (myclass, NULL, first, NULL, DEVICE_NAME)))
    {
      printk (KERN_ERR "SisCom: Error al crear el dispositivo %s.\n",
              DEVICE_NAME);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      return PTR_ERR (dev_ret);
    }

  /* Se agrega el dispositivo previamente creado al sistema */
  cdev_init (&c_dev, &pugs_fops);
  if ((ret = cdev_add (&c_dev, first, 1)) < 0)
    {
      printk (KERN_ERR
              "SisCom: Error al agregar el dispositivo al sistema %s.\n",
              DEVICE_NAME);
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      return ret;
    }

  /* Se registra el sensor 1 */
  ret = gpio_request_array (buttons1, ARRAY_SIZE (buttons1));
  if (ret)
    {
      printk (KERN_ERR "SisCom: Error al regustrar el sensor1.\n");
      cdev_del (&c_dev);
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      return ret;
    }

  /* Se registra el sensor 2 */
  ret = gpio_request_array (buttons2, ARRAY_SIZE (buttons2));
  if (ret)
    {
      printk (KERN_ERR "SisCom: Error al regustrar el sensor2.\n");
      cdev_del (&c_dev);
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      gpio_free_array (buttons1, ARRAY_SIZE (buttons1));
      return ret;
    }

  /* Se registran los leds */
  ret = gpio_request_array (leds, ARRAY_SIZE (leds));
  if (ret)
    {
      printk (KERN_ERR "SisCom: Error al regustrar los leds.\n");
      cdev_del (&c_dev);
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      gpio_free_array (buttons1, ARRAY_SIZE (buttons1));
      gpio_free_array (buttons2, ARRAY_SIZE (buttons2));
    }

  /* Se apagan todos los leds */
  for (i = 0; i < ARRAY_SIZE (leds); i++)
    {
      gpio_set_value (leds[i].gpio, 0);
    }

  /* Se enciende led 0 */
  gpio_set_value (leds[0].gpio, 1);

  return 0;
}

static void __exit
drv4_exit (void)
{
  int i;
  /* Se desvincula el dispositivo, luego se lo elimina */
  cdev_del (&c_dev);
  device_destroy (myclass, first);
  /* Se elimina la clase y por ultimo de desregistra major-minor*/
  class_destroy (myclass);
  unregister_chrdev_region (first, 1);

  /* Se desregistrans los sensores */
  gpio_free_array (buttons1, ARRAY_SIZE (buttons1));
  gpio_free_array (buttons2, ARRAY_SIZE (buttons2));

  /* Se apagan todos los leds */
  for (i = 0; i < ARRAY_SIZE (leds); i++)
    {
      gpio_set_value (leds[i].gpio, 0);
    }
  /* Se desregistrans los leds */
  gpio_free_array (leds, ARRAY_SIZE (leds));
}

static ssize_t
my_read (struct file *f, char __user *buf, size_t len, loff_t *off)
{
  char buff_aux[7];
  int value, length, i, num_pin, bool_pin, temp_selector;

  if (selector == 0)
    return 0;

  /* Se guarda el selector en una variable local por si cambia en el proceso de
   * calculo */
  temp_selector = selector;

  value = 0;
  num_pin = 4;

  if (temp_selector == 1)
    num_pin = 2;

  /* Se calcula el valor del sensor en decimal */
  for (i = 0; i < num_pin; i++)
    {
      if (temp_selector == 1)
        {
          bool_pin = gpio_get_value (buttons1[i].gpio);
        }
      else
        bool_pin = gpio_get_value (buttons2[i].gpio);

      if (bool_pin)
        {
          switch (i)
            {
            case 0:
              value = value + 1;
              break;
            case 1:
              value = value + 2;
              break;
            case 2:
              value = value + 4;
              break;
            case 3:
              value = value + 8;
              break;
            }
        }
    }

  /* Se guarda el valor obtenido a buff_aux */
  sprintf (buff_aux, "%d", value);

  length = strlen (buff_aux);
  if (len < length)
    {
      length = len;
    }

  /* Se envia el valor al dispositivo de caracter */
  if (*off == 0)
    {
      if (copy_to_user (buf, buff_aux, length) != 0)
        {
          return -EFAULT;
        }
      else
        {
          (*off)++;
          return length;
        }
    }
  else
    {
      return 0;
    }
}

static ssize_t
my_write (struct file *f, const char __user *buf, size_t len, loff_t *off)
{
  int i;
  char msg[15];
  selector = 0;

  if (len > 15)
    len = 15;

  /* Se copia el contenido del dispositivo a msg*/
  if (copy_from_user (msg, buf, len) != 0)
    return -EFAULT;

  /* Se verifica cual sensor eligio */
  if (strncmp (msg, "sensor1", 15) == 0)
    selector = 1;
  if (strncmp (msg, "sensor2", 15) == 0)
    selector = 2;

  /* Se apagan todos los leds y se prende el indicado */
  for (i = 0; i < ARRAY_SIZE (leds); i++)
    gpio_set_value (leds[i].gpio, 0);
  if (selector > -1 && selector < ARRAY_SIZE (leds))
    gpio_set_value (leds[selector].gpio, 1);

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
my_open (struct inode *i, struct file *f)
{
  return 0;
}
static int
my_close (struct inode *i, struct file *f)
{
  return 0;
}

module_init (drv4_init);
module_exit (drv4_exit);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Juan Ignacio Fernandez y Juan Pablo Saucedo");
MODULE_DESCRIPTION ("Trabajo final de SdeC");
