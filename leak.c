//#include <gnome.h>
#include <gtk/gtk.h>
#include <pcap.h>
#define __FAVOR_BSD
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#define PORT_WIDTH 256
#define PORT_HEIGHT 256
#define PACKETS_PER_PIXEL 10
#define VERSION "0.1"

#define QUANTIZE 16
#define CHANNELS 1
#define SAMPLERATE 44200
#define MAXPORT 6000

#include <sys/types.h> /* next 3 lines for open */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h> /* malloc */
#include <unistd.h> /* read()/write() */
#include <sys/ioctl.h> /* For IOCTL calls */
#include <linux/soundcard.h> /* For Audio Defs */
#include <stdio.h>
#include <math.h> /* for cos() */

guchar portplot_rgbbuf[PORT_WIDTH * PORT_HEIGHT * 3];
guchar countplot_rgbbuf[PORT_WIDTH * PORT_HEIGHT * 3];
guchar zoombuf[PORT_WIDTH * PORT_HEIGHT * 3];
GtkWidget *zoomscroll;
GtkObject *scroll_params;

/* GTK function primatives */
gboolean on_portplot_expose(GtkWidget *leak,
		 	 GdkEventExpose *event,
			 gpointer user_data);
gboolean on_countplot_expose(GtkWidget *leak,
		 	 GdkEventExpose *event,
			 gpointer user_data);
gboolean grap_location(GtkWidget *map,
			GdkEventButton *clicktype,
			gpointer user_data);
gboolean packet_received(GtkWidget *);
gboolean update_port(GtkWidget *, int, guchar, guchar, guchar);
gboolean update_portplot_map(GtkWidget *leak);
gboolean decay_portplot(GtkWidget *);
gboolean decay_countplot(GtkWidget *);
static gint delete_event_close(GtkWidget *mainwindow, GdkEventAny *e, gpointer data);
static void change_sound_state(GtkWidget *sndbutton, gpointer data);
static void change_zoom_state(GtkWidget *zmbutton, gpointer data);
static void change_scroll_value(GtkAdjustment *scroll, gpointer data);
static void display_about(void);

/* Audio support primatives */
int setuptone(void);
int tone(int port, int maxfreq, int tonelength);

int sound_fd; // File descriptor for sound card
gboolean sound_onoff;
gboolean zoom_onoff;

/* PCap globals needed */
struct pcap_pkthdr h;
struct pcap *pcap_d;
char ebuf[255];

/* Needed for count plot */
int packetcount;

int main (int argc, char **argv)
{
	GtkWidget *window, *table, *portplot_area_container, *countplot_area_container, *buttonbox;				// Containers
	GtkWidget *portplot, *countplot, *zoom, *zoomlabel, *soundonoff, *soundlabel, *about, *quit;	// Actual buttons
	gint x, y;
	guchar *pos;

	// Initialize variables
	packetcount = 0;
	sound_fd = -1;
	sound_onoff = FALSE;
	zoom_onoff = FALSE;
	

	//gnome_init("leak", VERSION, argc, argv);
	gtk_init(&argc, &argv);
	gdk_rgb_init();

	// Initialize the window
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW (window), "The leak");
	gtk_signal_connect(GTK_OBJECT (window), 
			"delete_event", 
			GTK_SIGNAL_FUNC(delete_event_close), 
			NULL);

	// Set up containers
	table = gtk_table_new(4, 1, FALSE);
	gtk_container_add(GTK_CONTAINER (window), table);

	buttonbox = gtk_vbox_new(FALSE, 0);
	gtk_table_attach(GTK_TABLE (table),
			buttonbox,
			3, 4,
			0, 1,
			GTK_FILL,
			GTK_FILL,
			0,
			0);

	// Set up drawing areas
	countplot_area_container = gtk_event_box_new();
	countplot = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER (countplot_area_container), countplot);
	gtk_container_set_border_width(GTK_CONTAINER (countplot_area_container), 2);
	gtk_drawing_area_size(GTK_DRAWING_AREA (countplot), 
				PORT_WIDTH, 
				PORT_HEIGHT);
	gtk_signal_connect(GTK_OBJECT (countplot), 
			"expose-event",
			GTK_SIGNAL_FUNC (on_countplot_expose), 
			NULL);
	gtk_table_attach(GTK_TABLE (table),
			countplot_area_container,
			0, 1,
			0, 1,
			GTK_FILL,
			GTK_FILL,
			0,
			0);

	portplot_area_container = gtk_event_box_new();
	portplot = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER (portplot_area_container), portplot);
	gtk_container_set_border_width(GTK_CONTAINER (portplot_area_container), 2);
	gtk_drawing_area_size(GTK_DRAWING_AREA (portplot), 
				PORT_WIDTH, 
				PORT_HEIGHT);
	gtk_signal_connect(GTK_OBJECT (portplot), 
			"expose-event",
			GTK_SIGNAL_FUNC (on_portplot_expose), 
			NULL);
	gtk_signal_connect(GTK_OBJECT (portplot_area_container),
			"button_press_event",
			GTK_SIGNAL_FUNC (grap_location),
			NULL);
	gtk_table_attach(GTK_TABLE (table),
			portplot_area_container,
			1, 2,
			0, 1,
			GTK_FILL,
			GTK_FILL,
			0,
			0);

	// Zoom button
	zoomlabel = gtk_label_new("Zoom In");
	zoom = gtk_button_new();
	gtk_container_add(GTK_CONTAINER (zoom), zoomlabel);
	gtk_box_pack_start(GTK_BOX (buttonbox),
			zoom,
			FALSE,
			FALSE,
			0);
	gtk_container_set_border_width(GTK_CONTAINER (zoom), 0);
	gtk_signal_connect(GTK_OBJECT (zoom),
			"clicked",
			GTK_SIGNAL_FUNC(change_zoom_state),
			zoomlabel);

	// Sound button
	soundlabel = gtk_label_new("Activate\nAudio");
	soundonoff = gtk_button_new();
	gtk_container_add(GTK_CONTAINER (soundonoff), soundlabel);
	gtk_container_set_border_width(GTK_CONTAINER (soundonoff), 0);
	gtk_box_pack_start(GTK_BOX (buttonbox),
			soundonoff,
			FALSE,
			FALSE,
			0);
	gtk_signal_connect(GTK_OBJECT (soundonoff),
			"clicked",
			GTK_SIGNAL_FUNC(change_sound_state),
			soundlabel);

	// About button
	// Commented out for lack of Gnome support
	/*
	about = gtk_button_new_with_label("About...");
	gtk_container_set_border_width(GTK_CONTAINER (about), 0);
	gtk_box_pack_start(GTK_BOX (buttonbox),
			about,
			FALSE,
			FALSE,
			0);
	gtk_signal_connect(GTK_OBJECT (about),
			"clicked",
			GTK_SIGNAL_FUNC(display_about),
			NULL);
	*/

	// Quit button
	quit = gtk_button_new_with_label("Quit");
	gtk_container_set_border_width(GTK_CONTAINER (quit), 0);
	gtk_box_pack_start(GTK_BOX (buttonbox),
			quit,
			FALSE,
			FALSE,
			0);
	gtk_signal_connect(GTK_OBJECT (quit),
			"clicked",
			GTK_SIGNAL_FUNC(delete_event_close),
			NULL);

	// Scroll bar
	scroll_params = gtk_adjustment_new(1.0,
					1,
					1,
					0,
					0,
					0);
	zoomscroll = gtk_vscrollbar_new(GTK_ADJUSTMENT (scroll_params));
	gtk_table_attach(GTK_TABLE (table),
			zoomscroll,
			2, 3,
			0, 1,
			GTK_FILL,
			GTK_FILL,
			0,
			0);
	gtk_range_set_update_policy(GTK_RANGE (zoomscroll),
				GTK_UPDATE_DISCONTINUOUS);
	gtk_signal_connect(GTK_OBJECT (scroll_params),
			"value_changed",
			GTK_SIGNAL_FUNC(change_scroll_value),
			portplot);
	gtk_widget_show_all (window);

	// The speed issues is due to the parameters passed
	// to open_live.  We have to optimize

	pcap_d = pcap_open_live("eth0",100,0,75,ebuf); 
  
	gtk_idle_add((GtkFunction)packet_received,portplot);
	gtk_timeout_add(200,(GtkFunction)decay_portplot,portplot); 
	gtk_timeout_add(3000, (GtkFunction)decay_countplot, countplot);
	gtk_main();
	return 0;
}

gboolean on_countplot_expose(GtkWidget *leak,
			GdkEventExpose *event,
			gpointer user_data)
{
	update_countplot_map(leak);
}

gboolean on_portplot_expose(GtkWidget *leak,
			GdkEventExpose *event,
			gpointer user_data)
{
	update_portplot_map(leak);
}

gboolean grap_location(GtkWidget *map,
			GdkEventButton *clicktype,
			gpointer user_data)
{
	gint x, y;
	gint portclicked;
	
	gdk_window_get_pointer(clicktype->window, &x, &y, NULL);
	fprintf(stderr, "x, y: (%i, %i)\n", x, y);

	if (zoom_onoff == FALSE) {
		portclicked = (x-1) + 256*(y-1);
		fprintf(stderr, "port: %i\n", portclicked);
	}

	if (zoom_onoff == TRUE) {
		portclicked = (x-1)/4 + (y-1)/4*64;	
		fprintf(stderr, "port: %i\n", portclicked);	
	}
}
	
gboolean decay_portplot(GtkWidget *leak)
{
	int i;

	for(i = 0; i < (PORT_HEIGHT*PORT_WIDTH*3); i++) {
		if(portplot_rgbbuf[i]>10) {
			portplot_rgbbuf[i]-=10;
		} else {
			portplot_rgbbuf[i]=0;
		}
    	}
	
	update_portplot_map(leak);
}

gboolean decay_countplot(GtkWidget *leak)
{
	int i, j;
	
	for (i = 0; i < PORT_WIDTH-1; i++) {
		for (j = 0; j < PORT_HEIGHT; j++) {
			countplot_rgbbuf[(i+j*PORT_WIDTH)*3] = countplot_rgbbuf[(i+1+j*PORT_WIDTH)*3];
			countplot_rgbbuf[(i+j*PORT_WIDTH)*3+1] = countplot_rgbbuf[(i+1+j*PORT_WIDTH)*3+1];
			countplot_rgbbuf[(i+j*PORT_WIDTH)*3+2] = countplot_rgbbuf[(i+1+j*PORT_WIDTH)*3+2];
			}
	}
	
	for (j = 0; j < PORT_HEIGHT; j++) {
		countplot_rgbbuf[(PORT_HEIGHT-j+1)*PORT_WIDTH*3-3] = 0;
		countplot_rgbbuf[(PORT_HEIGHT-j+1)*PORT_WIDTH*3-3+1] = 0;
		countplot_rgbbuf[(PORT_HEIGHT-j+1)*PORT_WIDTH*3-3+2] = 0;
	}

	packetcount = (packetcount / PACKETS_PER_PIXEL) + 1;

	if (packetcount < PORT_HEIGHT) {
		for (j = 0; j < packetcount; j++) {
			countplot_rgbbuf[(PORT_HEIGHT-j+1)*PORT_WIDTH*3-3] = 255;
		}
	} else {
		for (j = 0; j < PORT_HEIGHT; j++) {
			countplot_rgbbuf[(PORT_HEIGHT-j+1)*PORT_WIDTH*3-3] = 255;
		}
	}

	update_countplot_map(leak);
}

gboolean packet_received(GtkWidget *leak) {
	int i;
	char *buffer;
	struct tcphdr *tcp_hdr;
	struct udphdr *udp_hdr;
	struct ip *ip_hdr;
	int linkoff=14;


	buffer = (u_char *)pcap_next(pcap_d,&h);
	packetcount++;

	ip_hdr = (struct ip *)(buffer + linkoff);
	
	if (ip_hdr->ip_p == IPPROTO_TCP) {
		tcp_hdr=(struct tcphdr*)(((char*)ip_hdr)+(4*ip_hdr->ip_hl));

		switch ((tcp_hdr->th_flags)) {	

			case TH_ACK:		
				update_port(leak, ntohs(tcp_hdr->th_dport), 0, 255, 0);
				if (sound_onoff) {
					tone(ntohs(tcp_hdr->th_dport), 4000, 60);
				}
				break;

			case TH_SYN:
				update_port(leak, ntohs(tcp_hdr->th_dport), 255, 255, 0); if (sound_onoff) {
					tone(ntohs(tcp_hdr->th_dport), 4000, 60);
				}
				break;

			case TH_FIN:
				update_port(leak, ntohs(tcp_hdr->th_dport), 255, 0, 0);
				if (sound_onoff) {
					tone(ntohs(tcp_hdr->th_dport), 4000, 60);
				}
				break;

			default:
				break;

		}	
  	} else if (ip_hdr->ip_p == IPPROTO_UDP) {
		udp_hdr=(struct udphdr*)(((char*)ip_hdr)+(4*ip_hdr->ip_hl));

		update_port(leak, ntohs(udp_hdr->uh_dport), 0, 0, 255);
		if (sound_onoff) {
			tone(ntohs(udp_hdr->uh_dport), 4000, 60);
		}
	}
	
	
	return TRUE;
}

gboolean update_port(GtkWidget *leak, int port, guchar r, guchar g, guchar b) {
	portplot_rgbbuf[port*3] = r;
	portplot_rgbbuf[port*3+1] = g;
	portplot_rgbbuf[port*3+2] = b;

	update_portplot_map(leak);
}

gboolean update_countplot_map(GtkWidget *leak) {
	gdk_draw_rgb_image (leak->window,
		leak->style->fg_gc[GTK_STATE_NORMAL],
		0, 
		0, 
		PORT_WIDTH, 
		PORT_HEIGHT,
     		GDK_RGB_DITHER_MAX, 
		countplot_rgbbuf, 
		PORT_WIDTH * 3);
}

gboolean update_portplot_map(GtkWidget *leak) {
	int startport, i, j, k;
	int x, y, q;

	if (zoom_onoff == FALSE) {
		gdk_draw_rgb_image (leak->window,
			leak->style->fg_gc[GTK_STATE_NORMAL],
			0, 
			0, 
			PORT_WIDTH, 
			PORT_HEIGHT,
     			GDK_RGB_DITHER_MAX, 
			portplot_rgbbuf, 
			PORT_WIDTH * 3);
	} 

	if (zoom_onoff == TRUE) {
		startport = GTK_ADJUSTMENT (scroll_params)->value * 4096;

		for (i = startport; i < startport+4096; i = i++) {

			x = i % 64;
			y = i / 64;
	
			for (j = 0; j <= 3; j++) {
				for (k = 0; k <= 3; k++) {
					zoombuf[3*(x*4+j)+768*(4*y+k)] = portplot_rgbbuf[3*i];
					zoombuf[3*(x*4+j)+768*(4*y+k)+1] = portplot_rgbbuf[3*i+1];
					zoombuf[3*(x*4+j)+768*(4*y+k)+2] = portplot_rgbbuf[3*i+2];
				}
			}
		}

		gdk_draw_rgb_image (leak->window,
			leak->style->fg_gc[GTK_STATE_NORMAL],
			0, 
			0, 
			PORT_WIDTH, 
			PORT_HEIGHT,
     			GDK_RGB_DITHER_MAX, 
			zoombuf, 
			PORT_WIDTH * 3);
	}
}

static gint delete_event_close(GtkWidget *mainwindow, GdkEventAny *e, gpointer data) {
	gtk_main_quit();
	return FALSE;
}

static void change_sound_state(GtkWidget *sndbutton, gpointer data) {
	GtkWidget *sndlabel;

	sndlabel = GTK_WIDGET(data);

	if (sound_onoff == FALSE) {
		sound_onoff = TRUE;
		gtk_label_set_text(GTK_LABEL(sndlabel), "Deactivate\nAudio");
		
	} else {
		sound_onoff = FALSE;
		gtk_label_set_text(GTK_LABEL(sndlabel), "Activate\nAudio");
	}
}

static void change_zoom_state(GtkWidget *button, gpointer data) {
	GtkWidget *zmlabel;

	zmlabel = GTK_WIDGET(data);

	if (zoom_onoff == FALSE) {
		GTK_ADJUSTMENT (scroll_params)->value = 0;
		GTK_ADJUSTMENT (scroll_params)->lower = 0;
		GTK_ADJUSTMENT (scroll_params)->upper = 15;
		GTK_ADJUSTMENT (scroll_params)->step_increment = 1;
		GTK_ADJUSTMENT (scroll_params)->page_increment = 1;
		GTK_ADJUSTMENT (scroll_params)->page_size = 1;
		
		zoom_onoff = TRUE;
		gtk_label_set_text(GTK_LABEL(zmlabel), "Zoom Out");
		gtk_range_slider_update(GTK_RANGE (zoomscroll));
		
	} else {
		GTK_ADJUSTMENT (scroll_params)->value = 1;
		GTK_ADJUSTMENT (scroll_params)->lower = 1;
		GTK_ADJUSTMENT (scroll_params)->upper = 1;
		GTK_ADJUSTMENT (scroll_params)->step_increment = 0;
		GTK_ADJUSTMENT (scroll_params)->page_increment = 0;
		GTK_ADJUSTMENT (scroll_params)->page_size = 1;
		
		zoom_onoff = FALSE;
		gtk_label_set_text(GTK_LABEL(zmlabel), "Zoom In");
		gtk_range_slider_update(GTK_RANGE (zoomscroll));
	}
}

static void change_scroll_value(GtkAdjustment *scroll, gpointer data) {

	// This is to round the float to the nearest int.  Don't ask.
	
	if ((gfloat) scroll->value > (gfloat) ((gint) scroll->value) + .5) {
		scroll->value = (gint) scroll->value + 1;
	} else {
		scroll->value = (gint) scroll->value;
	}
	
	gtk_range_slider_update(GTK_RANGE (zoomscroll));

	update_portplot_map(GTK_WIDGET(data));
}

/*
static void display_about(void) {
	GtkWidget *about;
	const gchar *authors[] = {
		"xxx",
		"yyy",
		NULL
	};

	about = gnome_about_new ("The leak",
				VERSION,
				"stuff",
				authors,
				"Yellow = SYN\nGreen = ACK\nRed = FIN\nBlue = Any UDP Packet",
				NULL);
	gtk_window_set_modal(GTK_WINDOW (about), TRUE);
	gtk_widget_show (about);
}
*/

/* Sound Functions */

int setuptone(void) {
	unsigned long int arg;
	int status;


	sound_fd = open("/dev/dsp", O_RDWR);
	if (sound_fd < 0) {
		perror("/dev/dsp"); exit(1);
	}

	arg = QUANTIZE;
	status = ioctl(sound_fd, SOUND_PCM_WRITE_BITS, &arg);

	if (status == -1) {
		perror("SOUND_PCM_WRITE_BITS ioctl failed");
		exit(1);
	}
	if (arg != QUANTIZE) {
		perror("unable to set quantize rate");
		exit(1);
	}

	arg = SAMPLERATE;
	status = ioctl(sound_fd, SOUND_PCM_WRITE_RATE, &arg);
	if (status == -1) {
		perror("SOUND_PCM_WRITE_RATE ioctl failed");
		exit(1);
	}

	arg = CHANNELS;
	status = ioctl(sound_fd, SOUND_PCM_WRITE_CHANNELS, &arg);
	if (status == -1) {
		perror("SOUND_PCM_WRITE_CHANNELS ioctl failed");
		exit(1);
	}
	return 0;
}

int tone(int port, int maxfreq, int tonelength) {

	int n;
	unsigned long int i;
	unsigned long int bufsize, status;
	unsigned short int *buf;

	double frequency;
	unsigned int samplerate = 44200;
	unsigned long int samples = samplerate * tonelength/1000;

	if (sound_fd < 0) {
		setuptone();
	}
	
	bufsize = samples * sizeof(unsigned short int);
	buf = (unsigned short int *) malloc(bufsize);

	if (port > MAXPORT) {
		port = MAXPORT;
	}
	frequency = 2 * M_PI * (port*maxfreq/MAXPORT + 20);
        
	for (i = 0; i < samples/2; i++) {
		buf[i] = ((unsigned short int) (cos(frequency*i/samplerate) * 10000) * (i*2)/samples);
	}
	
	for (i = samples/2; i < samples; i++) {
		buf[i] = ((unsigned short int) (cos(frequency*i/samplerate) * 10000) * (-i*2 + 2*samples)/samples);
	}

	status = write(sound_fd, buf, bufsize);
	if (status != bufsize) {
		perror("wrote wrong number of bytes");
	}
	status = ioctl(sound_fd, SOUND_PCM_SYNC, 0);
	if (status == -1) {
		perror("SOUND_PCM_SYNC ioctl failed");
	}

	free((void *) buf);
	return 0;
}
