#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

// #include <linux/string.h>
// #include <linux/init.h>
// #include <linux/timer.h>
// #include <linux/jiffies.h>
#include <linux/gpio.h> //kmalloc y demas
// #include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>

static dev_t first;                  // Global variable for the first device number
static struct cdev c_dev;            // Global variable for the character device structure
static struct class *myclass = NULL; /*!< Clase */

/* Task handle to identify thread */
// static struct task_struct *ts = NULL;

/* Define BUTTONs */
static struct gpio buttons[] = {
{ 12, GPIOF_IN, "BUTTON 1" },
{ 13, GPIOF_IN, "BUTTON 2" },
{ 19, GPIOF_IN, "BUTTON 3" },
{ 26, GPIOF_IN, "BUTTON 4" },
};

#define DEVICE_NAME "siscom" /*!< Nombre del dispositivo (/proc/devices). */
#define BUF_LEN 128          /*!< Longitud maxima del mensaje. */

// static char *valor;
// static char v[BUF_LEN] = { 0 };
// static int disponible = 0;
// static int senal = 0;

// static int
// get_inputs (void *data)
// {
//   int valint;
//   printk (KERN_INFO "SisCom:Entre.\n");

//   // loop until killed ...
//   while (!kthread_should_stop ())
//     {
//       valint = 256;

//       sprintf (v, "%d ", valint);
//       disponible = 1;

//       printk (KERN_INFO "SisCom: Hola\n");
//       mdelay (5000);
//     }

//   return 0;
// }

/**
 * @brief Cambia el permiso del dispositivo a 0666.
 *
 * @param dev Puntero al dispositivo.
 * @param env Puntero a env.
 * @return int 0 en el caso que finalice correctamente.
 */
static int
mychardev_uevent(__attribute__((unused)) struct device *dev,
                 struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

static int my_open(struct inode *i, struct file *f)
{
    printk(KERN_INFO "SdeC_drv4: open()\n");
    // valor=v;
    return 0;
}
static int my_close(struct inode *i, struct file *f)
{
    printk(KERN_INFO "SdeC_drv4: close()\n");
    return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    // int bytes_read = 0;

    // if (disponible == 0)
    //     return 0;

    // /* Carga datos en buffer. */
    // while (len && *valor)
    // {
    //     put_user(*(valor++), buf++);
    //     len--;
    //     bytes_read++;
    // }
    // disponible = 0;

    char buff_aux[7];
    int valor, length, i;

    valor = 0;

    for (i = 0; i < 4; i++)
        {
          if (gpio_get_value (buttons[i].gpio))
            {
              switch (i)
              {
              case 0:
                  valor= valor + 1;
                  break;
               case 1:
                  valor= valor + 2;
                  break;
               case 2:
                  valor= valor + 4;
                  break;
               case 3:
                  valor= valor + 8;
                  break;
              default:
                  break;
            }
        }
        }

    sprintf (buff_aux, "%d", valor);


    length = strlen(buff_aux);
    if(len < length){
        length = len;
    }

    if (*off == 0) {
        if (copy_to_user(buf, buff_aux, length) != 0){
            return -EFAULT;
        }
        else {
            (*off)++;
            return length;
        }
    }
    else{
        return 0;
    }
}

// my_write escribe "len" bytes en "buf" y devuelve la cantidad de bytes escrita,
// que debe ser igual "len".

// Cuando hago un $ echo "bla bla bla..." > /dev/SdeC_drv3, se convoca a my_write.!!

static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    // senal++;
    return 0;
}

static struct file_operations pugs_fops =
    {
        .owner = THIS_MODULE,
        .open = my_open,
        .release = my_close,
        .read = my_read,
        .write = my_write};

static int __init drv4_init(void) /* Constructor */
{
    int ret;
    struct device *dev_ret;

    printk(KERN_INFO "SdeC: drv4 Registrado exitosamente..!!\n");

    // if ((valor = kmalloc(BUF_LEN, GFP_KERNEL)) == 0)
    // {
    //     printk(KERN_ALERT "SisCom: Fallo al crear variable valor:\n");
    //     return -1;
    // }

    // memset(valor, ' ', BUF_LEN);
    // sprintf(v, "%d ", 55);
    // disponible = 1;

    // El mayor lo da el so, 0-> menor, 1-> cantidad de disp; nombre del /proc/device
    if ((ret = alloc_chrdev_region(&first, 0, 1, DEVICE_NAME)) < 0)
    {
        
        
        return ret;
    }

    if (IS_ERR(myclass = class_create(THIS_MODULE, DEVICE_NAME)))
    {
        
        
        unregister_chrdev_region(first, 1);
        return PTR_ERR(myclass);
    }
    myclass->dev_uevent = mychardev_uevent;

    if (IS_ERR(dev_ret = device_create(myclass, NULL, first, NULL, DEVICE_NAME)))
    {
        
        
        class_destroy(myclass);
        unregister_chrdev_region(first, 1);
        return PTR_ERR(dev_ret);
    }

    cdev_init(&c_dev, &pugs_fops);
    if ((ret = cdev_add(&c_dev, first, 1)) < 0)
    {
        
        
        device_destroy(myclass, first);
        class_destroy(myclass);
        unregister_chrdev_region(first, 1);
        return ret;
    }

    ret = gpio_request_array (buttons, ARRAY_SIZE (buttons));

  if (ret)
    {
      printk (KERN_ERR "Unable to request GPIOs for BUTTONs: %d\n", ret);
      cdev_del(&c_dev);
      device_destroy(myclass, first);
        class_destroy(myclass);
        unregister_chrdev_region(first, 1);
      return ret;
    }

    // ts = kthread_create (get_inputs, NULL, "get_inputs");

  // if (ts)
  //   {
  //     wake_up_process (ts);
  //   }
  // else
  //   {
  //     printk (KERN_ERR "Unable to create thread\n");
  //     cdev_del(&c_dev);
  //     device_destroy(myclass, first);
  //       class_destroy(myclass);
  //       unregister_chrdev_region(first, 1);
  //         gpio_free_array (buttons, ARRAY_SIZE (buttons));
  //     return -1;
  //   }
    return 0;
}

static void __exit drv4_exit(void) /* Destructor */
{    
    cdev_del(&c_dev);
    device_destroy(myclass, first);
    class_destroy(myclass);
    unregister_chrdev_region(first, 1);
    // __clear_user (mensaje, BUF_LEN);
    // __clear_user (valor, BUF_LEN);
    
    // if (ts)
    // {
    //   kthread_stop (ts);
    //   printk (KERN_INFO "SisCom: Se libro hilo\n");
    // }
    
    printk(KERN_INFO "SdeC_drv4: dice Adios mundo kernel..!!\n");

    gpio_free_array (buttons, ARRAY_SIZE (buttons));
}

module_init(drv4_init);
module_exit(drv4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miguel Solinas <miguel.solinas@unc.edu.ar>");
MODULE_DESCRIPTION("Tercer driver de SdeC");
