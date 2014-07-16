/*
 * This file is part of RTRlib.
 *
 * RTRlib is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * RTRlib is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with RTRlib; see the file COPYING.LESSER.
 */

#include "rtrlib/spki/hashtable/ht-spkitable.h"

#include <string.h>
#include <stdio.h>
#include <pthread.h>

struct key_entry {
    uint8_t ski[SKI_SIZE];
    uint32_t asn;
    uint8_t spki[SPKI_SIZE];
    const struct rtr_socket *socket;
    struct key_entry *next;
    tommy_node hash_node;
    tommy_node list_node;
};



static int spki_record_cmp(const void *arg, const void *obj);
static int asn_cmp(const void *arg, const void *obj);


//****NOTE****
//All of the following code needs more error handling and of course testing, will come back to later



void spki_table_init(struct spki_table *spki_table) {
    tommy_hashlin_init(&spki_table->hashtable);
    tommy_list_init(&spki_table->list);
    pthread_rwlock_init(&spki_table->lock, NULL);
    spki_table->cmp_fp = spki_record_cmp;
}

void spki_table_free(struct spki_table *spki_table){

}

//TODO: Remove printf statements and do real output (notify clients?)
int spki_table_add_entry(struct spki_table *spki_table, struct spki_record *spki_record) {
    struct key_entry *entry = malloc(sizeof(struct key_entry));
    if(entry == NULL){
        return SPKI_ERROR;
    }

    entry->asn = spki_record->asn;
    entry->socket = spki_record->socket;
    entry->next = NULL;
    memcpy(entry->ski, spki_record->ski, SKI_SIZE);
    memcpy(entry->spki, spki_record->spki, SPKI_SIZE);

    uint32_t hash = tommy_inthash_u32(spki_record->asn);
    int rtval = SPKI_ERROR;

    pthread_rwlock_wrlock(&spki_table->lock);
    if(tommy_hashlin_search(&spki_table->hashtable, spki_table->cmp_fp, entry, hash)){
        rtval = SPKI_DUPLICATE_RECORD;
	} else{
		//Insert into hashtable and list
        tommy_hashlin_insert(&spki_table->hashtable, &entry->hash_node, entry, hash);
        tommy_list_insert_tail(&spki_table->list, &entry->list_node, entry);
        printf("Add Router Key: ASN: %u\n",spki_record->asn);
        rtval = SPKI_SUCCESS;
	}
    pthread_rwlock_unlock(&spki_table->lock);
	return rtval;
}


struct spki_record* spki_table_get_all(struct spki_table *spki_table, uint32_t asn) {
    uint32_t hash = tommy_inthash_u32(asn);

    pthread_rwlock_wrlock(&spki_table->lock);
    //A tommy node contains its storing spki_record (->data) as well as next and prev pointer
    //to accomodate multiple results
    tommy_node *result = tommy_hashlin_bucket(&spki_table->hashtable, hash);
    pthread_rwlock_unlock(&spki_table->lock);

    if(!result){
        return NULL;
    }


    //We link up the spki_record structs so we don't end up using tommy nodes outside of the keys files.
    struct spki_record *head = result->data;
    struct spki_record *current = head;
    while(result){
        current->next = result->next->data;
        result = result->next;
        current = current->next;
    }
    return head;
}

//TODO: Remove printf statements and do real output (notify clients?)
int spki_table_remove_entry(struct spki_table *spki_table, struct spki_record *spki_record) {

    uint32_t hash = tommy_inthash_u32(spki_record->asn);
    int rtval;

    pthread_rwlock_wrlock(&spki_table->lock);

    if(!tommy_hashlin_search(&spki_table->hashtable, spki_table->cmp_fp, spki_record, hash)){
        rtval = SPKI_RECORD_NOT_FOUND;
        printf("Could not remove Router Key: ASN: %u (key not found)\n",spki_record->asn);
    } else {

        //Remove from hashtable and list
        struct key_entry *rmv_elem = tommy_hashlin_remove(&spki_table->hashtable, spki_table->cmp_fp, spki_record, hash);
        if(rmv_elem && tommy_list_remove_existing(&spki_table->list, &rmv_elem->list_node)){
            rtval = SPKI_SUCCESS;
            printf("Removed Router Key: ASN: %u\n",rmv_elem->asn);
            free(rmv_elem);
        } else{
            rtval = SPKI_ERROR;
        }
    }

    pthread_rwlock_unlock(&spki_table->lock);
    free(spki_record);
    return rtval;
}


//Needs error handling and testing
int spki_table_src_remove(struct spki_table *spki_table, const struct rtr_socket *socket) {

    //Find all spki_record that have spki_record->socket == socket, link them together
    tommy_node *current_node = tommy_list_head(&spki_table->list);

    struct key_entry *del_head = NULL;
    struct key_entry *del_tail = NULL;
    struct key_entry *current_entry = NULL;


    while(current_node){
        current_entry = (struct key_entry *)current_node->data;

        if(current_entry->socket == socket){
            if(del_head){
                //Attach current_entry to the end, make it the new tail
                del_tail->next = current_entry;
                del_tail = current_entry;
                del_tail->next = NULL;
            } else {
                //If this is the first elem that maches the socket
                //Init head and tail
                del_head = current_entry;
                del_tail = current_entry;
                del_head->next = del_tail;
            }
        }
        //Iterate over spki_table->list
        current_node = current_node->next;
    }

    current_entry = del_head;

    struct spki_record *temp;

    //Remove all elements we found from hashtable and list, free them
    pthread_rwlock_wrlock(&spki_table->lock);
    while(current_entry){
        tommy_hashlin_remove_existing(&spki_table->hashtable, &current_entry->hash_node);
        tommy_list_remove_existing(&spki_table->list, &current_entry->list_node);
        temp = current_entry;
        current_entry = current_entry->next;
        free(temp);
    }
    pthread_rwlock_unlock(&spki_table->lock);
    return SPKI_SUCCESS;

}

//Local functions -------------------------

//TODO Add enum for rtvals?
static int spki_record_cmp(const void *arg, const void *obj) {
    struct spki_record *param = (struct spki_record*)arg;
    struct spki_record *entry = (struct spki_record*)obj;

	if(param->asn != entry->asn)
		return 1;
	if(memcmp(param->ski,entry->ski,SKI_SIZE))
		return 1;
	if(memcmp(param->spki,entry->spki,SPKI_SIZE))
		return 1;

	return 0;
}

static int asn_cmp(const void *arg, const void *obj) {
    return *(const uint32_t*)arg != ((const struct spki_record*)obj)->asn;
}





//TODO: Add validate_router_key function
//TODO: Add notify_clients function







