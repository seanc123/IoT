#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime.h"
#include "dev/button-sensor.h"
#include "stdbool.h"
#include <stdio.h>

#define BC_CHANNEL 130
#define MH_CHANNEL 131
#define DEFAULT_H 180
#define DEFAULT_PARENT_H 199
#define RSS_THRESHOLD -95


/* Structure of broadcast messages. */
struct message {
    char msg[10];
    uint8_t h;
};


/* Structure that holds information about neighbors. */
struct Parent {
    /* The addr field holds the Rime address of the neighbor. */
    rimeaddr_t addr;

    /* Current H value of the parent node */
    uint8_t h;
    
    /* The last_rssi field hold the Received Signal Strength Indicator (RSSI) that is received for the incoming broadcast packets. */
    uint16_t last_rssi;
    
};

/* Set up global variables */
static struct Parent parent;
static struct ctimer ct;
static uint8_t h_value;
static uint8_t destroy_bcast = 0;
static uint8_t build_bcast = 0;
static uint8_t send_data = 0;
static rimeaddr_t sink;



/* Broadcast & MultiHop structure. */
static struct broadcast_conn broadcast;
static struct multihop_conn multihop;

/* These two defines are used for computing the moving average for the broadcast sequence number gaps. */
#define SEQNO_EWMA_UNITY 0x100
#define SEQNO_EWMA_ALPHA 0x040

/*---------------------------------------------------------------------------*/
PROCESS(broadcast_process, "Broadcast process");
PROCESS(send_data_process, "Send Data Process");
AUTOSTART_PROCESSES(&broadcast_process, &send_data_process);
/*---------------------------------------------------------------------------*/


/*
 * This functions prepares the nodes for when the network is to be built/rebuilt.
 * If the nodes rime addr is 1.0, the h value is set to 0 to represent the sink, else it equals 180.
 * To ensure the most fitting parent is set for this node, this function also resets this nodes 
 * parents fields.
 */
static void prepare_node()
{
    if(rimeaddr_cmp(&sink, &rimeaddr_node_addr))
    {
    	h_value = 0;
        printf("%d.%d: Sink found. H value set to 0\n", rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1]);
    }
    else
    {
	printf("%d.%d: Node not sink, H set to default of %d\n", rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1], DEFAULT_H);
	h_value = DEFAULT_H;
	parent.addr = rimeaddr_null;
	parent.h = DEFAULT_PARENT_H;
	parent.last_rssi = RSS_THRESHOLD;
    }

}


/*
 * Toggles the type of broadcast being sent by the node from destroy to build and from build to nothing.
 */
static void toggle_bcast_type()
{
    if(destroy_bcast == 1 && build_bcast == 0){
        destroy_bcast = 0;
        build_bcast = 1;
	
    } else if(destroy_bcast == 0 && build_bcast == 1){
	destroy_bcast = 0;
	build_bcast = 0;
    }
}


/* 
 * This function is called whenever a broadcast message is received. 
 */
static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
    // Copy the packetbuf (broadcast messages contain the senders H value)
    void *ct_ptr = &ct;


    

    /* The packetbuf_dataptr() returns a pointer to the first data byte in the received packet. This will be a struct of type message */
    struct message *m;
    m = packetbuf_dataptr();

    // Get the real rss
    static uint16_t rss;
    static uint16_t rss_offset;
    rss_offset = -45;
    rss = packetbuf_attr(PACKETBUF_ATTR_RSSI) + rss_offset;

    /* Only run the following code for when a broadcast recieved if the current node is not the sink */
    if(!(rimeaddr_cmp(&sink, &rimeaddr_node_addr)))
    {
	if(strcmp(m->msg,"Destroy") == 0) {

	
	    if(destroy_bcast!=1){
	        prepare_node();
	        destroy_bcast = 1;
		send_data=0;
	        ctimer_set(&ct, CLOCK_SECOND*15, toggle_bcast_type, ct_ptr);
	    }

	} else if(strcmp(m->msg,"Build") == 0) {
	    
	    if(rss > RSS_THRESHOLD)
	    {
	        // If the senders H value equals this nodes parents H value.
		if (m->h == parent.h)
		{
		    // If the H values are the same but the sender has a stronger RSS then set the sender as this nodes parent.
		    if(rss > parent.last_rssi){
			rimeaddr_copy(&parent.addr, from);
			parent.h = m->h;
			parent.last_rssi = rss;
			h_value = m->h + 1;
			printf("New Parent Set:\nAddress - %d.%d\nH value - %d\nRSSI - %d\n", from->u8[0], from->u8[1], m->h, rss);
		    }
		} else if (m->h < parent.h){ // If the sender has a lower H value than this nodes parent, set this nodes parent as the sender.
		    rimeaddr_copy(&parent.addr, from);
		    parent.h = m->h;
		    parent.last_rssi = rss;
		    h_value = m->h + 1;
		    printf("New Parent Set:\nAddress - %d.%d\nH value - %d\nRSSI - %d\nNodes new H value:%d\n", from->u8[0], from->u8[1], m->h, rss, h_value);
		}
	    } 
	}
    }
}

/*---------------------------------------------------------------------------*/
/*
 * This function is called at the final recepient of the message.
 */
static void
recv(struct multihop_conn *c, const rimeaddr_t *sender,
     const rimeaddr_t *prevhop,
     uint8_t hops)
{
    printf("Message received from '%s'\n", (char *)packetbuf_dataptr());
}


/*
 * This function is called to forward a packet. The function forwards
 * a packet onto this nodes parent, printing out this node's address,
 * the targets address and the number of hops so far.
 */
static rimeaddr_t *
forward(struct multihop_conn *c,
	const rimeaddr_t *originator, const rimeaddr_t *dest,
	const rimeaddr_t *prevhop, uint8_t hops)
{
    printf("%d.%d:  To - %d.%d    Hops %d\n",
    rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1],
    parent.addr.u8[0], parent.addr.u8[1], 
    packetbuf_attr(PACKETBUF_ATTR_HOPS));
    return &parent.addr;

}


/* 
 * This is where we define what functions are to be called when a 
 * multi-hop packet is received or when a packet is to be forwarded
 * onto another node. We pass a pointer to this structure in the 
 * multihop_open() call below. 
 */
static const struct multihop_callbacks multihop_call = {recv, forward};

/* This is where we define what function to be called when a broadcast is received. We pass a pointer to this structure in the broadcast_open() call below. */
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};


PROCESS_THREAD(send_data_process, ev, data)
{
    

    PROCESS_EXITHANDLER(multihop_close(&multihop);)
    
    static char addr[3];

    PROCESS_BEGIN();
  
    rimeaddr_t to;

    /* Copy node address to addr string which will be copied to the packetbuf */
    sprintf(addr, "%x.%x", rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1]);

    /* Open a multihop connection on Rime channel MH_CHANNEL. */
    multihop_open(&multihop, MH_CHANNEL, &multihop_call);



    while(1) {
        /* Wait until we get a sensor event with the button sensor as data. */
        PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event &&
  			     data == &button_sensor);
        if(send_data == 1)
        {
	    /* Copy the node address to the packet buffer. */
	    packetbuf_copyfrom(addr, 4);

	    /* Set the Rime address of the final receiver of the packet to
	       1.0. This is a value that happens to work nicely in a Cooja
	       simulation (because the default simulation setup creates one
	       node with address 1.0). */
	    rimeaddr_copy(&to, &sink);

	    /* Send the packet. */
	    multihop_send(&multihop, &to);

        } else {
	    printf("Unable to send data, network being built\n");	  
	}

    }

    PROCESS_END();
}





PROCESS_THREAD(broadcast_process, ev, data)
{
    /* Initialize the sink address */
    sink.u8[0] = 1;
    sink.u8[1] = 0;

    static struct etimer et;
    void *ct_ptr = &ct;
    struct message msg;
    uint8_t loop;
    
    PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    
    PROCESS_BEGIN();


    /* Set the nodes H value and initialize its parent */
    prepare_node();

    broadcast_open(&broadcast, BC_CHANNEL, &broadcast_call);

    /* Activate the button sensor. We use the button to drive traffic -
       when the button is pressed, a packet is sent. */
    SENSORS_ACTIVATE(button_sensor);

    while(1) {

	/* The sink and normal nodes will have different operations to perform in the broadcast process */
	if(rimeaddr_cmp(&sink, &rimeaddr_node_addr))
    	{
	    /* By allowing this code in the program and removing the etimer wait at the end 
	     * of this if statement, the network can be destroyed and rebuilt be pressing 
	     * the button on the sink node
	     *
             * PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event &&
  	     *	             data == &button_sensor);
	     *
	     */
	    

	    /* Wait 5 seconds to allow all nodes to start up before sending destroy beacon */
	    etimer_set(&et, CLOCK_SECOND*5);
	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

	    printf("Network breakdown commenced, send data disabled\n");
	    strcpy(msg.msg, "Destroy");

	    packetbuf_copyfrom(&msg, sizeof(struct message));

	    broadcast_send(&broadcast);
		
	    /* Wait 50 seconds for network to be destroyed */
	    etimer_set(&et, CLOCK_SECOND * 40);
	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));


  	    printf("Network building commenced\n");

	    /* Loop variable keeps resetting to 0 after the etimer expires

	    loop=0;
	    while(loop != 3){
	        strcpy(msg.msg, "Build");
    	        msg.h = h_value;

	        packetbuf_copyfrom(&msg, sizeof(struct message));

	        broadcast_send(&broadcast);
		loop= loop+1;
		printf("1) Loop = %d\n", loop);
	        etimer_set(&et, CLOCK_SECOND*5);
	        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
		printf("2) Loop = %d\n", loop);

	    }
	    */
	     

	    strcpy(msg.msg, "Build");
    	    msg.h = h_value;

	    /* Broadcast the sinks H value 3 time */

	    packetbuf_copyfrom(&msg, sizeof(struct message));

	    broadcast_send(&broadcast);
	    etimer_set(&et, CLOCK_SECOND*5);
	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

	  
	    packetbuf_copyfrom(&msg, sizeof(struct message));
	    broadcast_send(&broadcast);
	    etimer_set(&et, CLOCK_SECOND*5);
	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));


	    packetbuf_copyfrom(&msg, sizeof(struct message));
	    broadcast_send(&broadcast);

	    printf("Broadcasting finished\n");

	    /* After sending the build beacon, wait 10 minutes before rebuilding the network again */
	    etimer_set(&et, CLOCK_SECOND*600);
	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

	} else {

	    if(destroy_bcast==1) {

		strcpy(msg.msg, "Destroy");
		packetbuf_copyfrom(&msg, sizeof(struct message));
		broadcast_send(&broadcast);

	        etimer_set(&et, CLOCK_SECOND*40);
	        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

		destroy_bcast=0;

	    } else if(build_bcast==1) {  

		/* Only build the network when the build_bcast flag has been set */
		ctimer_set(&ct, CLOCK_SECOND*45, toggle_bcast_type, ct_ptr);

		while(build_bcast==1){

		    strcpy(msg.msg, "Build");
	    	    msg.h = h_value;

		    packetbuf_copyfrom(&msg, sizeof(struct message));

		    broadcast_send(&broadcast);

		    /* Send a broadcast every 3 - 6 seconds */
		    etimer_set(&et, CLOCK_SECOND * 3 + random_rand() % (CLOCK_SECOND * 3));
		    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

		}

		printf("BUILD FINISHED, SEND DATA ENABLED\n");
		send_data=1;

	    }


            /* If the build_bcast flag is not set, wait 5 seconds before checking to to see if it has been set */
	    etimer_set(&et, CLOCK_SECOND*5);
	    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

	}

	

    }
    
    PROCESS_END();
}
