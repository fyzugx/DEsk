/* DEsk Wood Maintenance v0.1.8: lasting scars and safe Restore action.
 * These scars are a visual overlay; the underlying desktop is untouched. */
/* Wood Maintenance 0.1.7: small, versioned, per-user cosmetic damage record.
 * Coordinates are saved as screen fractions, so a changed resolution does
 * not leave old marks beyond the edge. No window or application is altered. */
static void termites_store_scar(struct desk_server *, struct termite_agent *);
static void termites_nibble(cairo_t *, double, double, double, uint32_t, int, bool);

static bool termites_scar_file(char *out, size_t cap)
{
	const char *base = getenv("XDG_STATE_HOME");
	const char *home = getenv("HOME");
	int n;
	if (base && base[0] == '/')
		n = snprintf(out, cap, "%s/desk/wood-damage-v1.txt", base);
	else if (home && home[0] == '/')
		n = snprintf(out, cap, "%s/.local/state/desk/wood-damage-v1.txt", home);
	else
		return false;
	return n > 0 && (size_t)n < cap;
}

static void termites_scar_dirs(void)
{
	char path[1024];
	if (!termites_scar_file(path, sizeof path)) return;
	for (char *p = path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(path, 0700) != 0 && errno != EEXIST) {
				*p = '/';
				return;
			}
			*p = '/';
		}
	}
}

static void termites_damage_save(struct desk_server *server)
{
	if (!server->conf.termites_damage_persistent) return;
	char path[1024];
	if (!termites_scar_file(path, sizeof path)) return;
	if (!server->termites_scar_count) { unlink(path); return; }
	if (server->desk_w <= 0 || server->desk_h <= 0) return;
	termites_scar_dirs();
	char tmp[1100];
	int n = snprintf(tmp, sizeof tmp, "%s.tmp-XXXXXX", path);
	if (n < 0 || (size_t)n >= sizeof tmp) return;
	int fd = mkstemp(tmp);
	if (fd < 0) return;
	FILE *f = fdopen(fd, "w");
	if (!f) { close(fd); unlink(tmp); return; }
	bool ok = fprintf(f, "DESK_WOOD_DAMAGE_V1\n") > 0;
	for (int i = 0; i < server->termites_scar_count && ok; i++) {
		const struct termite_scar *m = &server->termites_scars[i];
		ok = fprintf(f, "%.9g %.9g %.7g %u %d %d\n",
			m->x / server->desk_w, m->y / server->desk_h,
			m->bite, m->seed, m->edge, m->paper ? 1 : 0) > 0;
	}
	ok = fflush(f) == 0 && !ferror(f) && ok;
	if (ok) ok = fsync(fd) == 0;
	if (fclose(f) != 0) ok = false;
	if (!ok || rename(tmp, path) != 0) unlink(tmp);
}

static void termites_damage_load(struct desk_server *server)
{
	if (server->termites_scars_loaded) return;
	if (server->desk_w < 100 || server->desk_h < 100) return;
	server->termites_scars_loaded = true;
	server->termites_scars_dirty = true;
	if (!server->conf.termites_damage_persistent) return;
	char path[1024];
	if (!termites_scar_file(path, sizeof path)) return;
	FILE *f = fopen(path, "re");
	if (!f) return;
	char line[256];
	if (fgets(line, sizeof line, f) && strcmp(line, "DESK_WOOD_DAMAGE_V1\n") == 0) {
		while (fgets(line, sizeof line, f)
			&& server->termites_scar_count < TERMITES_SCAR_LIMIT) {
			double x, y, bite;
			unsigned seed;
			int edge, paper;
			if (sscanf(line, "%lf %lf %lf %u %d %d", &x, &y, &bite,
				&seed, &edge, &paper) != 6
				|| !isfinite(x) || !isfinite(y) || !isfinite(bite)
				|| x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0
				|| bite < 0.035 || bite > 1.0 || edge < 0 || edge > 4
				|| (paper != 0 && paper != 1)) continue;
			struct termite_scar *m = &server->termites_scars[server->termites_scar_count++];
			*m = (struct termite_scar){ x * server->desk_w,
				y * server->desk_h, bite, seed, edge, paper != 0 };
		}
	}
	fclose(f);
}

static void termites_damage_clear(struct desk_server *server)
{
	server->termites_scar_count = 0;
	server->termites_scar_next = 0;
	server->termites_scars_loaded = true;
	server->termites_scars_dirty = true;
	for (int i = 0; i < server->termites_count; i++)
		server->termites_agents[i].bite = 0.0;
	char path[1024];
	if (termites_scar_file(path, sizeof path)) unlink(path);
	if (server->termites_scar_node)
		wlr_scene_node_set_enabled(&server->termites_scar_node->node, false);
	/* Startup reads shell.conf before the output list or scene exists. */
	if (server->layer_termites) server_schedule_frames(server);
}

/* Static paper/wood scratches: painted only after a completed bite, a restore,
 * or a resolution change, never every idle frame. */
static void termites_damage_draw(struct desk_server *server)
{
	if (!server->layer_termites || server->desk_w < 100 || server->desk_h < 100)
		return;
	termites_damage_load(server);
	if (server->termites_scar_w != server->desk_w
		|| server->termites_scar_h != server->desk_h) {
		/* Keep existing cosmetic scars in the same relative places on resize. */
		if (server->termites_scar_w > 0 && server->termites_scar_h > 0) {
			double sx = (double)server->desk_w / server->termites_scar_w;
			double sy = (double)server->desk_h / server->termites_scar_h;
			for (int i = 0; i < server->termites_scar_count; i++) {
				server->termites_scars[i].x *= sx;
				server->termites_scars[i].y *= sy;
			}
		}
		server->termites_scar_w = server->desk_w;
		server->termites_scar_h = server->desk_h;
		server->termites_scars_dirty = true;
	}
	if (server->termites_scars_dirty) {
		server->termites_scars_dirty = false;
		if (server->termites_scar_count) {
			double k = fmin(1.0, 1152.0 / server->desk_w);
			struct desk_buf *b = buf_create((int)ceil(server->desk_w * k),
				(int)ceil(server->desk_h * k));
			cairo_scale(b->cr, k, k);
			for (int i = 0; i < server->termites_scar_count; i++) {
				const struct termite_scar *m = &server->termites_scars[i];
				termites_nibble(b->cr, m->x, m->y, m->bite,
					m->seed, m->edge, m->paper);
			}
			scene_put(&server->termites_scar_node, server->layer_termites,
				b, 0, 0, NULL);
			if (server->termites_scar_node) {
				wlr_scene_buffer_set_dest_size(server->termites_scar_node,
					server->desk_w, server->desk_h);
				server->termites_scar_node->point_accepts_input = shadow_accepts;
				wlr_scene_node_lower_to_bottom(&server->termites_scar_node->node);
			}
		}
	}
	bool show = server->termites_scar_count > 0 && !server->dark
		&& !locked(server) && server->shelf_t <= 0.1 && !server->headless;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link)
		if (view->fullscreen && view->scene_tree && view->scene_tree->node.enabled)
			show = false;
	if (server->termites_scar_node)
		wlr_scene_node_set_enabled(&server->termites_scar_node->node, show);
}

static void termites_hide(struct desk_server *server)
{
	/* A final batch is recorded after the frame's earlier scar repaint.
	 * Make that batch visible and request one presentation frame. */
	bool had_agents = server->termites_count > 0;
	if (server->termite_node)
		wlr_scene_node_set_enabled(&server->termite_node->node, false);
	server->termites_active = false;
	for (int i = 0; i < server->termites_count; i++)
		termites_store_scar(server, &server->termites_agents[i]);
	server->termites_count = 0;
	server->termites_session_key = 0;
	if (!server->conf.termites_damage_persistent && server->termites_scar_count)
		termites_damage_clear(server);
	else if (had_agents && server->conf.termites_damage_persistent) {
		termites_damage_draw(server);
		server_schedule_frames(server);
	}
	server->termites_scatter_start = 0.0;
	server->termites_scatter_until = 0.0;
}

/* A real pointer move startles ANY visible infestation, not only a Preview.
 * Freeze the agents and their scars; animate their departure separately.
 * Repeated pointer events must NOT extend or restart the departure. */
static void termites_disturb(struct desk_server *server)
{
	if (server->termites_scatter_until > 0.0) return;
	if (!server->termites_preview_active
		&& (!server->termites_active || server->termites_count <= 0)) return;
	double now = real_sec();
	server->termites_preview_active = false;
	server->termites_scatter_start = now;
	server->termites_scatter_until = now + 1.15;
	server->termites_last = now;
	server_schedule_frames(server);
}


static void termites_dot(cairo_t *cr, double x, double y, double r)
{
	cairo_arc(cr, x, y, r, 0, 2 * M_PI);
	cairo_fill(cr);
}

/* The body is deliberately small: irregular steps do the animation work. */
static void termites_bug(cairo_t *cr, double x, double y, double a, double t, double size)
{
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_rotate(cr, a);
	cairo_scale(cr, size, size);
	cairo_set_source_rgba(cr, 0.08, 0.05, 0.025, 0.30);
	cairo_save(cr);
	cairo_translate(cr, 0.7, 1.15);
	cairo_scale(cr, 1.65, 0.72);
	termites_dot(cr, 0, 0, 1.0);
	cairo_restore(cr);
	cairo_set_line_width(cr, 0.22);
	cairo_set_source_rgb(cr, 0.52, 0.31, 0.13);
	for (int side = -1; side <= 1; side += 2) {
		for (int leg = 0; leg < 3; leg++) {
			double bx = -1.0 + leg * 0.95;
			double step = sin(t * 10.0 + leg * 2.2 + side * 1.2) * 0.38;
			cairo_move_to(cr, bx, side * 0.33);
			cairo_line_to(cr, bx + step - 0.15, side * 1.35);
			cairo_line_to(cr, bx + step + 0.45, side * 1.65);
		}
	}
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 0.73, 0.49, 0.22);
	cairo_save(cr); cairo_scale(cr, 1.42, 0.83);
	termites_dot(cr, -0.55, 0, 1.05);
	cairo_restore(cr);
	cairo_set_source_rgb(cr, 0.90, 0.67, 0.34);
	termites_dot(cr, 0.55, 0, 0.76);
	cairo_set_source_rgb(cr, 0.63, 0.39, 0.16);
	termites_dot(cr, 1.43, 0, 0.56);
	cairo_set_line_width(cr, 0.17);
	for (int s = -1; s <= 1; s += 2) {
		cairo_move_to(cr, 1.8, s * 0.2);
		cairo_curve_to(cr, 2.0, s * 0.8, 2.6, s * 1.25, 2.8, s * 1.1);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* Individual PRNG state prevents two insects from inheriting the same route.
 * The seed changes on every idle/preview session. */
static uint32_t termites_rand(uint32_t *state)
{
	uint32_t x = *state ? *state : 0x6d2b79f5u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static double termites_unit(uint32_t *state)
{
	return (double)(termites_rand(state) & 0x00ffffffu) / 16777216.0;
}

struct termite_snack { double x, y, w, h; bool folded; };
static void termites_node_xy(const struct wlr_scene_node *node, double *x, double *y)
{
	*x = *y = 0;
	while (node) {
		*x += node->x;
		*y += node->y;
		node = node->parent ? &node->parent->node : NULL;
	}
}

/* Available paper edges, never the content of an application. */
static int termites_snacks(struct desk_server *server, struct termite_snack *out, int cap)
{
	int n = 0;
	struct desk_toplevel *v;
	if (server->conf.termites_chew_folded) {
		wl_list_for_each(v, &server->toplevels, link) {
			const struct desk_sheet *s = &v->sheet;
			if (n >= cap) break;
			if (s->phase != SHEET_PARKED || s->in_drawer || !s->tree
				|| !s->tree->node.enabled || s->hit_w < 34 || s->hit_h < 20) continue;
			double x, y;
			termites_node_xy(&s->tree->node, &x, &y);
			x += 8.0; y += 8.0;
			if (x > server->desk_w || y > server->desk_h || x + s->hit_w < 0 || y + s->hit_h < 0)
				continue;
			out[n++] = (struct termite_snack){ x, y, s->hit_w, s->hit_h, true };
		}
	}
	if (server->conf.termites_chew_windows) {
		wl_list_for_each(v, &server->toplevels, link) {
			if (n >= cap) break;
			if (!v->scene_tree || !v->scene_tree->node.enabled || v->fullscreen || v->unmanaged
				|| v->sheet.phase != SHEET_IDLE || v->outer_w < 95 || v->outer_h < 70)
				continue;
			double x, y;
			termites_node_xy(&v->scene_tree->node, &x, &y);
			if (x > server->desk_w || y > server->desk_h || x + v->outer_w < 0 || y + v->outer_h < 0)
				continue;
			out[n++] = (struct termite_snack){ x, y, v->outer_w, v->outer_h, false };
		}
	}
	return n;
}

/* Store a finished chewing spot independently of the termite who made it.
 * The array is bounded and the oldest marks are replaced only if a VERY long
 * infestation runs out of room, not whenever an insect chooses another spot. */
static void termites_store_scar(struct desk_server *server, struct termite_agent *a)
{
	if (a->bite < 0.035) return;
	int slot;
	if (server->termites_scar_count < TERMITES_SCAR_LIMIT) {
		slot = server->termites_scar_count++;
	} else {
		slot = server->termites_scar_next++ % TERMITES_SCAR_LIMIT;
	}
	server->termites_scars[slot] = (struct termite_scar){
		.x = a->tx, .y = a->ty, .bite = a->bite,
		.seed = a->mark_seed, .edge = a->target_edge,
		.paper = a->target_paper
	};
	server->termites_scars_dirty = true;
	termites_damage_save(server);
}

/* target_edge: 0 top, 1 right, 2 bottom, 3 left, 4 wooden desk.
 * Pick a DIFFERENT part of a plausible edge for each agent, every session. */
static void termites_pick(struct desk_server *server, struct termite_agent *a,
	const struct termite_snack *snacks, int count)
{
	termites_store_scar(server, a);
	double u = termites_unit(&a->rng);
	if (count > 0 && u < 0.72) {
		const struct termite_snack *s = &snacks[termites_rand(&a->rng) % (unsigned)count];
		a->target_edge = (int)(termites_rand(&a->rng) % 4u);
		a->target_paper = true;
		double f = 0.12 + 0.76 * termites_unit(&a->rng);
		switch (a->target_edge) {
		case 0: a->tx = s->x + s->w * f; a->ty = s->y + 3.0; break;
		case 1: a->tx = s->x + s->w - 3.0; a->ty = s->y + s->h * f; break;
		case 2: a->tx = s->x + s->w * f; a->ty = s->y + s->h - 3.0; break;
		default: a->tx = s->x + 3.0; a->ty = s->y + s->h * f; break;
		}
	} else {
		a->target_paper = false;
		a->target_edge = 4;
		a->tx = server->desk_w * (0.10 + 0.80 * termites_unit(&a->rng));
		a->ty = server->desk_h * (0.57 + 0.34 * termites_unit(&a->rng));
	}
	a->tx = clampf_d(a->tx, 12.0, server->desk_w - 12.0);
	a->ty = clampf_d(a->ty, 12.0, server->desk_h - 12.0);
	a->bite = 0.0;
	a->mark_seed = termites_rand(&a->rng); /* stable crumb pattern for this site */
	a->bursts = 0;
}

static void termites_spawn(struct desk_server *server, struct termite_agent *a,
	const struct termite_snack *snacks, int count)
{
	memset(a, 0, sizeof *a);
	a->rng = termites_rand(&server->termites_seed);
	termites_pick(server, a, snacks, count);
	double angle = termites_unit(&a->rng) * 2.0 * M_PI;
	double far = 38.0 + 95.0 * termites_unit(&a->rng);
	a->x = clampf_d(a->tx + cos(angle) * far, 5.0, server->desk_w - 5.0);
	a->y = clampf_d(a->ty + sin(angle) * far, 5.0, server->desk_h - 5.0);
	a->pace = 35.0 + 40.0 * termites_unit(&a->rng);
	a->heading = atan2(a->ty - a->y, a->tx - a->x);
	a->stage = 0;
}

/* WALK, INSPECT, CHEW, PAUSE. Insects stop at the target rather than orbiting.
 * Chewing consists of short, irregular bursts with little repositioning. */
static void termites_step(struct desk_server *server, struct termite_agent *a, double now,
	double dt, const struct termite_snack *snacks, int count)
{
	switch (a->stage) {
	case 0: { /* WALK */
		double dx = a->tx - a->x, dy = a->ty - a->y;
		double distance = hypot(dx, dy);
		if (distance <= 2.6) {
			a->x = a->tx; a->y = a->ty;
			a->stage = 1;
			a->event_at = now + 0.15 + 0.48 * termites_unit(&a->rng);
			break;
		}
		double move = fmin(distance, dt * a->pace);
		double lateral = (termites_unit(&a->rng) - 0.5) * fmin(1.5, move * 0.10);
		a->x += move * dx / distance - lateral * dy / distance;
		a->y += move * dy / distance + lateral * dx / distance;
		a->heading = atan2(dy, dx);
		a->legs += move * 0.16;
		break;
	}
	case 1: /* INSPECT */
		if (now >= a->event_at) {
			a->stage = 2;
			a->event_at = now + 0.30 + 0.70 * termites_unit(&a->rng);
		}
		break;
	case 2: /* CHEW */
		a->bite = fmin(1.0, a->bite + dt * (0.09 + 0.05 * termites_unit(&a->rng)));
		a->legs += dt * 0.45; /* tiny mouth/antenna motion, not a running gait */
		if (now >= a->event_at) {
			a->bursts++;
			a->stage = 3;
			a->event_at = now + 0.20 + 0.85 * termites_unit(&a->rng);
		}
		break;
	default: /* PAUSE: inspect again, shuffle along the edge, or scout elsewhere */
		if (now < a->event_at) break;
		if (a->bursts >= 3 && termites_unit(&a->rng) < 0.42) {
			termites_pick(server, a, snacks, count);
			a->stage = 0;
		} else if (termites_unit(&a->rng) < 0.52) {
			termites_store_scar(server, a);
			a->bite = 0.0;
			a->mark_seed = termites_rand(&a->rng);
			double drift = (termites_unit(&a->rng) - 0.5) * 27.0;
			if (a->target_edge == 0 || a->target_edge == 2 || a->target_edge == 4)
				a->tx = clampf_d(a->tx + drift, 12.0, server->desk_w - 12.0);
			else
				a->ty = clampf_d(a->ty + drift, 12.0, server->desk_h - 12.0);
			a->stage = 0;
		} else {
			a->stage = 2;
			a->event_at = now + 0.30 + 0.80 * termites_unit(&a->rng);
		}
		break;
	}
}

/* One cosmetic nibble, in screen coordinates. Seed is fixed per site.
 * This is overlay paint, never a hole in an application surface. */
static void termites_nibble(cairo_t *cr, double x, double y, double bite,
	uint32_t seed0, int edge, bool paper)
{
	if (bite < 0.035) return;
	if (paper) {
		double radius = 1.4 + 9.0 * bite;
		cairo_save(cr);
		cairo_translate(cr, x, y);
		switch (edge) {
		case 0: cairo_rotate(cr, M_PI * 0.5); break;
		case 1: cairo_rotate(cr, M_PI); break;
		case 2: cairo_rotate(cr, -M_PI * 0.5); break;
		default: break;
		}
		cairo_set_source_rgba(cr, 0.24, 0.13, 0.067, 0.91);
		cairo_move_to(cr, 0, -radius * 0.75);
		cairo_line_to(cr, radius * 0.4, -radius * 0.35);
		cairo_line_to(cr, radius * 0.92, -radius * 0.06);
		cairo_line_to(cr, radius * 0.75, radius * 0.47);
		cairo_line_to(cr, radius * 0.15, radius * 0.63);
		cairo_line_to(cr, 0, radius * 0.75);
		cairo_close_path(cr);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.82, 0.63, 0.32, 0.55);
		cairo_set_line_width(cr, 0.8);
		cairo_move_to(cr, radius * 0.1, radius * 0.7);
		cairo_line_to(cr, radius * 0.6, radius * 0.45);
		cairo_stroke(cr);
		cairo_restore(cr);
	} else { /* wood: crumbs and a small worn spot, not a black 'hole' */
		cairo_set_source_rgba(cr, 0.17, 0.08, 0.025, 0.23 * bite);
		cairo_arc(cr, x, y, 2.0 + 5.0 * bite, 0, 2.0 * M_PI);
		cairo_fill(cr);
	}
	for (int j = 0; j < 5; j++) {
		uint32_t seed = seed0 ^ ((uint32_t)(j+1) * 0x9e3779b9u);
		double dx = (termites_unit(&seed) - 0.5) * 21.0;
		double dy = (termites_unit(&seed) - 0.5) * 16.0;
		cairo_set_source_rgba(cr, 0.78, 0.57, 0.28, 0.25 + 0.32 * bite);
		termites_dot(cr, x + dx, y + dy, (0.35 + 0.6 * termites_unit(&seed)) * bite);
	}
}

static void termites_tick(struct desk_server *server)
{
	termites_damage_draw(server);
	double now = real_sec();
	bool preview = server->termites_preview_active;
	bool scatter = now < server->termites_scatter_until;
	if ((!server->conf.termites_enabled && !preview && !scatter) || !server->layer_termites
		|| server->dark || locked(server) || server->idle_held || server->shelf_t > 0.1
		|| server->desk_w < 100 || server->desk_h < 100) {
		termites_hide(server);
		return;
	}
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v->fullscreen && v->scene_tree && v->scene_tree->node.enabled) {
			termites_hide(server);
			return;
		}
	}
	double age = preview ? now - server->termites_preview_at
		: now - server->idle_at - server->conf.termites_wait;
	if (age < 0.0 && !scatter) {
		termites_hide(server);
		return;
	}
	/* A moving pointer makes the colony scatter off screen before it vanishes.
	 * No extra termites spawn during this farewell. */
	if (scatter) {
		double dt = clampf_d(now - server->termites_last, 0.0, 0.20);
		server->termites_last = now;
		for (int i = 0; i < server->termites_count; i++) {
			struct termite_agent *a = &server->termites_agents[i];
			double dx = a->x - server->cursor->x;
			double dy = a->y - server->cursor->y;
			if (hypot(dx, dy) < 5.0) { dx = (i & 1) ? 1.0 : -1.0; dy = 0.5; }
			a->heading = atan2(dy, dx);
			double speed = 210.0 + 47.0 * (i % 4);
			a->x += cos(a->heading) * speed * dt;
			a->y += sin(a->heading) * speed * dt;
			a->legs += speed * dt * 0.17;
		}
		double k = fmin(1.0, 1152.0 / server->desk_w);
		struct desk_buf *buf = buf_create((int)ceil(server->desk_w * k),
			(int)ceil(server->desk_h * k));
		cairo_scale(buf->cr, k, k);
		/* Completed marks are on their own permanent static overlay. */
		for (int i = 0; i < server->termites_count; i++) {
			const struct termite_agent *a = &server->termites_agents[i];
			termites_nibble(buf->cr, a->tx, a->ty, a->bite,
				a->mark_seed, a->target_edge, a->target_paper);
		}
		for (int i = 0; i < server->termites_count; i++) {
			struct termite_agent *a = &server->termites_agents[i];
			termites_bug(buf->cr, a->x, a->y, a->heading, a->legs, 2.05 + (i % 4)*0.29);
		}
		scene_put(&server->termite_node, server->layer_termites, buf, 0, 0, NULL);
		if (server->termite_node) {
			wlr_scene_buffer_set_dest_size(server->termite_node, server->desk_w, server->desk_h);
			server->termite_node->point_accepts_input = shadow_accepts;
			/* Hold full visibility through most of the run, fade in the
			 * final fraction only. A 0.65s fade hid the fleeing bugs. */
			wlr_scene_buffer_set_opacity(server->termite_node,
				(float)clampf_d((server->termites_scatter_until - now) / 0.30, 0.0, 1.0));
			wlr_scene_node_set_enabled(&server->termite_node->node, true);
		}
		server->termites_active = true;
		server_schedule_frames(server);
		return;
	}
	if (server->termites_scatter_until > 0.0) {
		server->termites_scatter_until = 0.0;
		termites_hide(server);
		return;
	}
	/* A click on Preview uses the current setting immediately. In idle mode
	 * the visitors enter gradually; changing the level adjusts count NOW. */
	uint64_t key = preview ? ((uint64_t)server->termites_preview_seen << 1) | 1u
		: ((uint64_t)(server->idle_at * 1000.0) << 1);
	if (key != server->termites_session_key || preview != server->termites_preview_mode) {
		server->termites_session_key = key;
		server->termites_preview_mode = preview;
		server->termites_count = 0;
		server->termites_level_seen = server->conf.termites_level;
		server->termites_last = now;
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		server->termites_seed = (uint32_t)ts.tv_nsec ^ (uint32_t)ts.tv_sec
			^ (uint32_t)(uintptr_t)server ^ (uint32_t)server->termites_preview_seen;
		if (!server->termites_seed) server->termites_seed = 0x5bd1e995u;
	}
	int cap = server->conf.termites_level == 0 ? 1
		: server->conf.termites_level == 2 ? 18 : 6;
	int wanted = preview ? cap : 1 + (int)floor(age / (cap == 1 ? 1000000.0 : 2.4));
	if (wanted > cap) wanted = cap;
	if (server->termites_level_seen != server->conf.termites_level) {
		/* No need to re-enter idle or click preview to see the change. */
		wanted = cap;
		server->termites_level_seen = server->conf.termites_level;
	}
	if (server->termites_count > wanted) server->termites_count = wanted;
	struct termite_snack snacks[24];
	int n = termites_snacks(server, snacks, 24);
	while (server->termites_count < wanted) {
		termites_spawn(server, &server->termites_agents[server->termites_count], snacks, n);
		server->termites_count++;
	}
	double dt = clampf_d(now - server->termites_last, 0.0, 0.20);
	server->termites_last = now;
	for (int i = 0; i < server->termites_count; i++)
		termites_step(server, &server->termites_agents[i], now, dt, snacks, n);
	/* Small transparent overlay instead of redrawing the compositor scene. */
	double k = fmin(1.0, 1152.0 / server->desk_w);
	int w = (int)ceil(server->desk_w * k), h = (int)ceil(server->desk_h * k);
	struct desk_buf *buf = buf_create(w, h);
	cairo_t *cr = buf->cr;
	cairo_scale(cr, k, k);
	/* Completed bite marks live on a cached independent scene node. */
	for (int i = 0; i < server->termites_count; i++) {
		const struct termite_agent *a = &server->termites_agents[i];
		termites_nibble(cr, a->tx, a->ty, a->bite, a->mark_seed,
			a->target_edge, a->target_paper);
	}
	for (int i = 0; i < server->termites_count; i++) {
		struct termite_agent *a = &server->termites_agents[i];
		bool feeding = a->stage == 2 || a->stage == 3;
		double x = a->x, y = a->y, heading = a->heading;
		if (feeding) {
			/* Nervous nibbles, never a circle or orbit around the target. */
			x += (termites_unit(&a->rng) - 0.5) * 0.9;
			y += (termites_unit(&a->rng) - 0.5) * 0.9;
			if (a->target_paper) {
				switch (a->target_edge) {
				case 0: heading = M_PI * 0.5; break;
				case 1: heading = M_PI; break;
				case 2: heading = -M_PI * 0.5; break;
				default: heading = 0.0; break;
				}
			}
		}
		termites_bug(cr, x, y, heading, a->legs, 2.05 + (i%4)*0.29);
	}
	scene_put(&server->termite_node, server->layer_termites, buf, 0, 0, NULL);
	if (server->termite_node) {
		wlr_scene_buffer_set_dest_size(server->termite_node, server->desk_w, server->desk_h);
		server->termite_node->point_accepts_input = shadow_accepts;
		wlr_scene_buffer_set_opacity(server->termite_node, 1.0f);
		wlr_scene_node_set_enabled(&server->termite_node->node, true);
	}
	server->termites_active = true;
	server_schedule_frames(server);
}