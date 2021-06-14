#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>

// #include <linux/string.h>
// #include <linux/init.h>
// #include <linux/timer.h>
// #include <linux/jiffies.h>
#include <linux/gpio.h> //kmalloc y demas
// #include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>

static dev_t first;       // Global variable for the first device number
static struct cdev c_dev; // Global variable for the character device structure
static struct class *myclass = NULL; /*!< Clase */

static int selector = 1;

/* Task handle to identify thread */
// static struct task_struct *ts = NULL;

/* Define BUTTONs */
static struct gpio buttons1[] = {
  { 10, GPIOF_IN, "BUTTON 1 1" },
  { 11, GPIOF_IN, "BUTTON 1 2" },
};

static struct gpio buttons2[] = {
  { 12, GPIOF_IN, "BUTTON 2 1" },
  { 13, GPIOF_IN, "BUTTON 2 2" },
  { 19, GPIOF_IN, "BUTTON 2 3" },
  { 26, GPIOF_IN, "BUTTON 2 4" },
};

#define DEVICE_NAME "siscom" /*!< Nombre del dispositivo (/proc/devices). */
#define BUF_LEN 128          /*!< Longitud maxima del mensaje. */

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
  //   printk (KERN_INFO "SdeC_drv4: open()\n");
  return 0;
}
static int
my_close (struct inode *i, struct file *f)
{
  //   printk (KERN_INFO "SdeC_drv4: close()\n");
  return 0;
}

static ssize_t
my_read (struct file *f, char __user *buf, size_t len, loff_t *off)
{
  char buff_aux[7];
  int value, length, i, num_pin, bool_pin, temp_selector;

  temp_selector = selector;
  value = 0;
  num_pin = 4;

  if (temp_selector == 0)
    return 0;

  if (temp_selector == 1)
    num_pin = 2;

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
            default:
              break;
            }
        }
    }

  sprintf (buff_aux, "%d", value);

  length = strlen (buff_aux);
  if (len < length)
    {
      length = len;
    }

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
  selector++;
  if (selector == 3)
    {
      selector = 0;
    }
  return len;
}

static struct file_operations pugs_fops = { .owner = THIS_MODULE,
                                            .open = my_open,
                                            .release = my_close,
                                            .read = my_read,
                                            .write = my_write };

static int __init
drv4_init (void) /* Constructor */
{
  int ret;
  struct device *dev_ret;

  printk (KERN_INFO "SdeC: drv4 Registrado exitosamente..!!\n");

  if ((ret = alloc_chrdev_region (&first, 0, 1, DEVICE_NAME)) < 0)
    {
      return ret;
    }

  if (IS_ERR (myclass = class_create (THIS_MODULE, DEVICE_NAME)))
    {
      unregister_chrdev_region (first, 1);
      return PTR_ERR (myclass);
    }
  myclass->dev_uevent = mychardev_uevent;

  if (IS_ERR (dev_ret
              = device_create (myclass, NULL, first, NULL, DEVICE_NAME)))
    {
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      return PTR_ERR (dev_ret);
    }

  cdev_init (&c_dev, &pugs_fops);
  if ((ret = cdev_add (&c_dev, first, 1)) < 0)
    {
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      return ret;
    }
  ret = gpio_request_array (buttons1, ARRAY_SIZE (buttons1));

  if (ret)
    {
      printk (KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
      cdev_del (&c_dev);
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      return ret;
    }
  ret = gpio_request_array (buttons2, ARRAY_SIZE (buttons2));

  if (ret)
    {
      printk (KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
      cdev_del (&c_dev);
      device_destroy (myclass, first);
      class_destroy (myclass);
      unregister_chrdev_region (first, 1);
      gpio_free_array (buttons1, ARRAY_SIZE (buttons1));
      return ret;
    }
  return 0;
}

static void __exit
drv4_exit (void) /* Destructor */
{
  cdev_del (&c_dev);
  device_destroy (myclass, first);
  class_destroy (myclass);
  unregister_chrdev_region (first, 1);

  printk (KERN_INFO "SdeC_drv4: dice Adios mundo kernel..!!\n");

  gpio_free_array (buttons1, ARRAY_SIZE (buttons1));
  gpio_free_array (buttons2, ARRAY_SIZE (buttons2));
}

module_init (drv4_init);
module_exit (drv4_exit);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Juan Ignacio Fernandez y Juan Pablo Saucedo");
MODULE_DESCRIPTION ("Trabajo final de SdeC");
