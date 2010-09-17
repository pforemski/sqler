/*
  just a small test app for libsmtp
   
Copyright © 2001 Kevin Read <obsidian@berlios.de>

This software is available under the GNU Lesser Public License as described
in the COPYING file.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Kevin Read <obsidian@berlios.de>
Thu Aug 16 2001 */



/*******************************************************************
 * This is a demonstration for libsmtp - it is not intended to be  *
 * good coding style - it's artificial. You have been warned :)    *
 * All good coders, cover your eyes now ;)			   *
 *******************************************************************/

#include <glib.h>
#include <stdio.h>

#include "libsmtp.h"

/* We need a function to read a line from stdin that cuts of the newline */

int read_line (char *buffer, int length)
{
  char *pointer;
  fgets (buffer, 255, stdin);
  
  buffer[strlen(buffer)-1]='\0';
  
/*  if ((pointer=strchr (buffer, '\n')) == NULL)
    return 0;
  
  pointer='\0'; */
  return 0;
}
  
/* To report all available information to the user */

int report_error (struct libsmtp_session_struct *mailsession)
{
 printf ("An error occured while connecting:\n%s\nLast Response:%s\n", \
    libsmtp_strerr (mailsession), mailsession->LastResponse->str);
  printf ("\nAll in all %u bytes of dialogue data (%u lines) were sent.\n",
    mailsession->DialogueBytes, mailsession->DialogueSent);
  printf ("%u bytes of header data (%u lines) were sent.\n",
     mailsession->HeaderBytes, mailsession->HeadersSent);
  printf ("%lu bytes of body data were sent.\n", mailsession->BodyBytes);

  return mailsession->ErrorCode;
}

int main(void)
{
  int test_temp=0, loop=1;
  char sender[255], subject[255], to[255], body[255], server[255];
  
  /* This struct holds all session data. You need one per mail server
     connection */
  struct libsmtp_session_struct *mailsession;
  
  /* We need these to look through the GLists later on */
  GList *temp_glist;
  GList *temp_glist_address;

  /* This mallocs the structs mem and initializes variables in it */
  mailsession = libsmtp_session_initialize();

  printf ("This is the libsmtp demonstration program for plain SMTP mails. You will now\nbe asked for a sender address, recipient addresses, the SMTP server to use,\na subject line and then the body data.\n\n");
  printf ("Please enter sender address (From: address): ");
  
  read_line (sender, 255);
  
  printf ("\nPlease enter subject line: ");
  read_line (subject, 255);
  
  printf ("\nPlease enter the hostname of your mail server: ");
  read_line (server, 255);
  
  /* Set session environment (from address, subject) */
  libsmtp_set_environment (sender, subject, 0, mailsession);
  
  printf ("Please enter recipients. You can enter one per line. If you finished adding\nrecipients, just press enter without entering data.\n");
  
  /* Now we add some To: recipients */
  while (loop)
  {
    printf ("To: ");
    read_line (to, 255);
    loop=strlen(to);
    if (loop)
    {
      printf ("Adding %s\n", to);
      if (libsmtp_add_recipient (LIBSMTP_REC_TO, to, mailsession))
        return report_error (mailsession);
    }
  }
  
  for (loop=0; loop < g_list_length (mailsession->To); loop++)
  {
    printf ("%d. Recipient: %s\n", loop+1, g_list_nth_data (mailsession->To, loop));
  }
  
  loop=1;
  
  printf ("\n\nNow you can enter email addresses that will receive carbon copies. Enter one\nper line. If you have finished entering addresses, just press enter without\ntyping data.\n");

  /* Now we add some Cc: recipients */
  while (loop)
  {
    printf ("Cc: ");
    read_line (to, 255);
    loop=strlen(to);
    if (loop)
      if (libsmtp_add_recipient (LIBSMTP_REC_CC, to, mailsession))
        return report_error (mailsession);      
  }
  
  loop=1;
  
  printf ("\n\nNow you can enter email addresses that will receive blind carbon copies.\nEnter one per line. If you have finished entering addresses, just press enter\n without typing data.\n");

  /* Now we add some Bcc: recipients */
  while (loop)
  {
    printf ("Bcc: ");
    read_line (to, 255);
    loop=strlen(to);
    if (loop)
      if (libsmtp_add_recipient (LIBSMTP_REC_BCC, to, mailsession))
        return report_error (mailsession);
  }

  /* This starts the SMTP connection */
  if (libsmtp_connect ("container", 0, 0, mailsession))
    return report_error (mailsession);
    
  /* This will conduct the SMTP dialogue */
  if (libsmtp_dialogue (mailsession))
    return report_error (mailsession);
  
  /* Now lets send the headers - you can send your own headers too */
  if (libsmtp_headers (mailsession))
    return report_error (mailsession);

  /* Now comes the main input loop */
  
  loop=1;
  printf ("\n\nNow type as many lines of body data as you want. If you are finished, type\nEND on a line by itself and press enter.\n\n");
  
  while (loop)
  {
    read_line (body, 255);
    if ( (body[0] == 'E') && (body[1] == 'N') && (body[2] == 'D') && \
         (strlen(body) == 3))
    {
      loop=0;
    }
    else
    {
      /* This sends a line of message body */
      if (libsmtp_body_send_raw (body, strlen (body), mailsession))
        return report_error (mailsession);
    }
  }
  
  /* This ends the body part */
  if (libsmtp_body_end (mailsession))
    return report_error (mailsession);
  
  /* This ends the connection gracefully */
  if (libsmtp_quit (mailsession))
  {
    /* Actually there can't be an error here... :) */
    return report_error (mailsession);
  }

  /* Ok, lets print some stats */
  printf ("Mail sent successfully.\nLast Response:%s\n", \
      mailsession->LastResponse->str);
  printf ("\nAll in all %u bytes of dialogue data (%u lines) were sent.\n",
      mailsession->DialogueBytes, mailsession->DialogueSent);
  printf ("%u bytes of header data (%u lines) were sent.\n",
       mailsession->HeaderBytes, mailsession->HeadersSent);
  printf ("%lu bytes of body data were sent.\n", mailsession->BodyBytes);
  

  /* This lists the contents of the struct delivery response codes */
  printf ("The mail could not be delivered to the following recipients:\n");
  
  if (mailsession->NumFailedTo)
  {
    for (test_temp=0; test_temp<g_list_length (mailsession->ToResponse); test_temp++)
    {
      temp_glist = g_list_nth (mailsession->ToResponse, test_temp);
      if (atoi (temp_glist->data) > 299)
      {
        temp_glist_address = g_list_nth (mailsession->To, test_temp);
        printf ("%s: %s\n", temp_glist_address->data, temp_glist->data);
      }
    }
  }
  
  if (mailsession->NumFailedCC)
  {
    for (test_temp=0; test_temp<g_list_length (mailsession->CCResponse); test_temp++)
    {
      temp_glist = g_list_nth (mailsession->CCResponse, test_temp);
      if (atoi (temp_glist->data) > 299)
      {
        temp_glist_address = g_list_nth (mailsession->CC, test_temp);
        printf ("%s: %s\n", temp_glist_address->data, temp_glist->data);
      }
    }
  }

  if (mailsession->NumFailedBCC)
  {
    for (test_temp=0; test_temp<g_list_length (mailsession->BCCResponse); test_temp++)
    {
      temp_glist = g_list_nth (mailsession->BCCResponse, test_temp);
      if (atoi (temp_glist->data) > 299)
      {
        temp_glist_address = g_list_nth (mailsession->BCC, test_temp);
        printf ("%s: %s\n", temp_glist_address->data, temp_glist->data);
      }
    }
  }    
  
  /* Free the allocated struct mem */
  libsmtp_free (mailsession);
  return 0;
}
