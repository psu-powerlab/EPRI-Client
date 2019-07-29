char notification_uri[64];
Acceptor *n_acceptor;
int n_secure = 1;

void subscribe_init (char *name, int ipv4, int secure) {
  Uri uri = {0}; Address host;
  char zero[16] = {0}; int port;
  if (ipv4) ipv4_address (&host, 0, 0);
  else ipv6_address (&host, zero, 0);
  n_acceptor = net_listen (&host);
  n_secure = secure;
  net_local (&host, n_acceptor);
  port = address_port (&host);
  interface_address (&host, name, ipv4);
  set_port (&host, port);
  uri.scheme = secure? "https" : "http";
  uri.host = &host;
  uri.path = "/notify";
  write_uri (notification_uri, &uri);
  printf ("subscribe_init: uri = %s\n", notification_uri);
  se_accept (n_acceptor, secure);
}

void subscribe (Stub *s, char *uri) {
  if (!s->subscribed) {
    SE_Subscription_t sub = {0};
    sub.subscribedResource = resource_name (s);
    sub.encoding = 0; // XML
    sprintf (sub.level, "-%s", se_schema.schemaId);
    sub.limit = 10;
    sub.notificationURI = notification_uri;
    se_post (s->conn, &sub, SE_Subscription, uri);
    // use context for updating the subscribed field
    set_request_context (s->conn, s);
  }
}

int match_notifier (void *conn, void *lfdi) {
  if (http_client (conn)) {
    printf ("match_notifier: "); print_bytes (lfdi, 20);
    printf (" "); print_bytes (se_lfdi (conn), 20); printf ("\n");
    return memcmp (se_lfdi (conn), lfdi, 20) == 0;
  } return 0;
}

void notification (void *conn, SE_Notification_t *n, DepFunc dep) {
  Stub *s, *head; Uri128 buf; Uri *uri = &buf.uri; void *client;
  if (http_parse_uri (&buf, conn, n->subscribedResource, 127)) {
    if (conn_secure (conn)) {
      client = find_conn (match_notifier, se_lfdi (conn));
      s = find_stub (&head, uri->path, client);
    } else if (s = find_resource (uri->path))
      client = s->conn;
    if (!(client && s)) goto close; 
    switch (n->status) {
    case 0: // Default Status
      if (se_exists (n, Resource)) {
	int rt = resource_type (s);
	if (element_type (rt, &se_schema) == n->Resource.type) {
	  void *obj = n->Resource.data;
	  s->base.time = time (NULL);
	  if (s->base.info) list_object (s, obj, dep);
	  else update_existing (s, obj, dep);
	  n->Resource.data = NULL;
	}
      } break;
    case 2: // Subscription canceled, resource moved
      if (n->newResourceURI) {
	get_moved (s, n->newResourceURI);
      }
    case 1: // Subscription canceled, no additional information
    case 3: // Subscription canceled, resource definition changed
      // (e.g., a new version of IEEE 2030.5)
      s->subscribed = 0; break;
    case 4: // Subscription canceled, resource deleted
      insert_event (s, RESOURCE_REMOVE, 0);
    } return;
  } close: conn_close (conn);
}

void process_notifications (void *conn, DepFunc dep) {
  char *path; void *obj; int type; List *l;
  SE_NotificationList_t *nl;
  switch (se_receive (conn)) {
  case HTTP_POST:
    path = http_path (conn);
    printf ("process_notification %s\n", path);
    if (obj = se_body (conn, &type)) {
      print_se_object (obj, type); printf ("\n");
      if (streq (path, "/notify")) {
	switch (type) {
	case SE_NotificationList: nl = obj;
	  foreach (l, nl->Notification)
	    notification (conn, l->data, dep);
	  break;
	case SE_Notification:
	  notification (conn, obj, dep); break;
	} http_respond (conn, 204);
      } free_se_object (obj, type);
    }
  }
}

void accept_notifier (void *conn) {
  printf ("accept_notifier\n");
  se_accept (n_acceptor, n_secure);
}
