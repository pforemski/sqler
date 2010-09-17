/*
  just a small test app for libsmtp mime functions
   
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
#include <stdlib.h>
#include <stdio.h>

#include "libsmtp.h"
#include "libsmtp_mime.h"


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

/* This will spew out status messages */

int stat_message (struct libsmtp_session_struct *mailsession)
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
  int test_temp=0, jpegfile_read;
  FILE *jpegfile;
  unsigned char *jpegbuffer, temp_string[256], sender[256], subject[256];
  unsigned char server[256], to[256];
  
  /* This struct holds all session data. You need one per mail server
     connection */
  struct libsmtp_session_struct *mailsession;
  struct libsmtp_part_struct *mainpart, *bodypart, *temppart;
  
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

  printf ("\nPlease enter the mail recipient: ");
  read_line (to, 255);

  /* Now we add some recipients */
  libsmtp_add_recipient (LIBSMTP_REC_TO, to, mailsession);

  printf ("1 recipient added.\n");

  /* Lets add some parts to the body. We gotta save the pointer to the
     main part */
  mainpart = libsmtp_part_new (NULL, LIBSMTP_MIME_MULTIPART, LIBSMTP_MIME_SUB_MIXED, LIBSMTP_ENC_7BIT, LIBSMTP_CHARSET_NOCHARSET, "Test MIME main part", mailsession);
  if (mainpart == NULL)
  {
    printf ("Error adding part: %s\n", libsmtp_strerr (mailsession));
    return 1;
  }

  /* The pointer to all other parts aren't significant to us */
  if (!libsmtp_part_new (mainpart, LIBSMTP_MIME_TEXT, LIBSMTP_MIME_SUB_PLAIN, LIBSMTP_ENC_7BIT, LIBSMTP_CHARSET_USASCII, "Test MIME text part", mailsession))
  {
    printf ("Error adding part: %s\n", libsmtp_strerr (mailsession));
    return 1;
  }
  
  if (!libsmtp_part_new (mainpart, LIBSMTP_MIME_TEXT, LIBSMTP_MIME_SUB_HTML, LIBSMTP_ENC_7BIT, LIBSMTP_CHARSET_USASCII, "Test MIME HTML part", mailsession))
  {
    printf ("Error adding part: %s\n", libsmtp_strerr (mailsession));
    return 1;
  }
  
  if (!libsmtp_part_new (mainpart, LIBSMTP_MIME_IMAGE, LIBSMTP_MIME_SUB_JPG, LIBSMTP_ENC_BASE64, LIBSMTP_CHARSET_NOCHARSET, "Test MIME Image part", mailsession))
  {
    printf ("Error adding part: %s\n", libsmtp_strerr (mailsession));
    return 1;
  }
  
  if (!libsmtp_part_new (mainpart, LIBSMTP_MIME_TEXT, LIBSMTP_MIME_SUB_PLAIN, LIBSMTP_ENC_QUOTED, LIBSMTP_CHARSET_ISO8859_1, "Test MIME text part (eightbit)", mailsession))
  {
    printf ("Error adding part: %s\n", libsmtp_strerr (mailsession));
    return 1;
  }
 
  printf ("Parts added.\n");

  /* Lets read the JPEG file we need to test Base64 encoding */
  if (!(jpegfile=fopen("../gnu-head-sm.jpg", "r")))
  {
    printf ("Error reading JPG file.\n");
    return 1;
  }

  /* This starts the SMTP connection */
  if (libsmtp_connect (server,0,0,mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }
  
  printf ("SMTP connection running.\n");
    
  /* This will conduct the SMTP dialogue */
  if (libsmtp_dialogue (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }
  
  printf ("Dialogue finished.\n");

  /* Now lets send the headers - you can send your own headers too */
  if (libsmtp_headers (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("SMTP headers sent.\n");

  /* Now lets send the MIME headers */
  if (libsmtp_mime_headers (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("MIME headers sent.\n");

  /* This sends a line of message body */
  strcpy (temp_string, "This is a text in plaintext.\n");

  if (libsmtp_part_send (temp_string, strlen (temp_string), mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }
  
  printf ("First part data sent.\n");

  /* This will switch to the next body part */
  if (libsmtp_part_next (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("Now in second body part.\n");

  /* What we need is a little test HTML */
  strcpy (temp_string, "<html><body><h1>Blurb.</h1></body></html>");

  /* Lets talk a little HTML */
  if (libsmtp_part_send (temp_string, strlen (temp_string), mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("Second part sent.\n");

  /* This will switch to the third body part */
  if (libsmtp_part_next (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("Now in third body part.\n");
  
  /* We need to read the jpeg file */
  
  jpegbuffer=malloc (4098);
  
  /* Read the file in blocks of 300 byte */
  while ((jpegfile_read = fread (jpegbuffer, 1, 300, jpegfile)))
  {
    /* Then we send each chunk */
    if (libsmtp_part_send (jpegbuffer, jpegfile_read-1, mailsession))
    {
      stat_message (mailsession);
      return mailsession->ErrorCode;
    }
  }
  
  fclose (jpegfile);
  free (jpegbuffer);

  printf ("Third part sent.\n");  

  /* This will switch to the next body part */
  if (libsmtp_part_next (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("Now in fourth body part.\n");

  /* What we need is a little test eightbit blurb */
  strcpy (temp_string, "üöä blurb!üößäÄ!5&9§$");

  /* Lets send it!! */
  if (libsmtp_part_send (temp_string, strlen (temp_string), mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }

  printf ("Fourth part sent.\n");
  
  /* This ends the body part */
  if (libsmtp_body_end (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
  }
  
  /* This ends the connection gracefully */
  if (libsmtp_quit (mailsession))
  {
    stat_message (mailsession);
    return mailsession->ErrorCode;
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
