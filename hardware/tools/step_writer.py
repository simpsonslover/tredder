"""
Minimal STEP (ISO 10303-21, AP214) writer for parts made of boxes.

Every box is a real B-rep solid (MANIFOLD_SOLID_BREP, 6 planar faces) with a
colour, so KiCad's STEP export can merge it with the board and Fusion (or any
CAD tool) imports it as a solid body you can assign a material to and mesh for
simulation. VRML models cannot do this: KiCad drops them from STEP exports.

Units are mm. No CAD kernel is needed (none is installed on this machine).
"""
import math


class Step:
    def __init__(self, name):
        self.name = name
        self.ents = []
        self.solids = []      # (entity id, rgb)

    def _e(self, text):
        self.ents.append(text)
        return f"#{len(self.ents)}"

    @staticmethod
    def _f(v):
        return f"{v:.6f}"

    def _pt(self, p):
        return self._e("CARTESIAN_POINT('',(" + ",".join(self._f(c) for c in p) + "))")

    def _dir(self, d):
        return self._e("DIRECTION('',(" + ",".join(self._f(c) for c in d) + "))")

    def box(self, x0, y0, z0, x1, y1, z1, rgb, name=""):
        """Axis-aligned box from corner (x0,y0,z0) to (x1,y1,z1)."""
        lo = (min(x0, x1), min(y0, y1), min(z0, z1))
        hi = (max(x0, x1), max(y0, y1), max(z0, z1))
        if any(h - l < 1e-6 for l, h in zip(lo, hi)):
            raise ValueError(f"{self.name}/{name}: zero-size box")
        corner = [tuple(hi[k] if (i >> k) & 1 else lo[k] for k in range(3)) for i in range(8)]
        vert = [self._e(f"VERTEX_POINT('',{self._pt(c)})") for c in corner]
        edges = {}

        def edge(a, b):
            key = (min(a, b), max(a, b))
            if key not in edges:
                pa, pb = corner[key[0]], corner[key[1]]
                d = [q - p for p, q in zip(pa, pb)]
                n = math.sqrt(sum(c * c for c in d))
                vec = self._e(f"VECTOR('',{self._dir([c / n for c in d])},{self._f(n)})")
                line = self._e(f"LINE('',{self._pt(pa)},{vec})")
                edges[key] = self._e(f"EDGE_CURVE('',{vert[key[0]]},{vert[key[1]]},{line},.T.)")
            return self._e(f"ORIENTED_EDGE('',*,*,{edges[key]},{'.T.' if (a, b) == key else '.F.'})")

        faces = []
        for k in range(3):
            u, v = (k + 1) % 3, (k + 2) % 3
            for side in (0, 1):
                # corners with coordinate k on this side, counter-clockwise around +k
                ring = [(0, 0), (1, 0), (1, 1), (0, 1)]
                idx = [(side << k) | (a << u) | (b << v) for a, b in ring]
                if side == 0:
                    idx.reverse()           # outward normal is -k: walk the other way
                normal = [0.0, 0.0, 0.0]
                normal[k] = 1.0 if side else -1.0
                ref = [0.0, 0.0, 0.0]
                ref[u] = 1.0
                loop = self._e("EDGE_LOOP('',(" + ",".join(edge(idx[i], idx[(i + 1) % 4]) for i in range(4)) + "))")
                bound = self._e(f"FACE_OUTER_BOUND('',{loop},.T.)")
                ax = self._e(f"AXIS2_PLACEMENT_3D('',{self._pt(corner[idx[0]])},{self._dir(normal)},{self._dir(ref)})")
                plane = self._e(f"PLANE('',{ax})")
                faces.append(self._e(f"ADVANCED_FACE('',({bound}),{plane},.T.)"))
        shell = self._e("CLOSED_SHELL('',(" + ",".join(faces) + "))")
        solid = self._e(f"MANIFOLD_SOLID_BREP('{name}',{shell})")
        self.solids.append((solid, rgb))
        return solid

    def write(self, path):
        e = self._e
        ctx_app = e("APPLICATION_CONTEXT('core data for automotive mechanical design processes')")
        e(f"APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,{ctx_app})")
        pctx = e(f"PRODUCT_CONTEXT('',{ctx_app},'mechanical')")
        prod = e(f"PRODUCT('{self.name}','{self.name}','',({pctx}))")
        pdf = e(f"PRODUCT_DEFINITION_FORMATION('','',{prod})")
        pdc = e(f"PRODUCT_DEFINITION_CONTEXT('part definition',{ctx_app},'design')")
        pd = e(f"PRODUCT_DEFINITION('design','',{pdf},{pdc})")
        pds = e(f"PRODUCT_DEFINITION_SHAPE('','',{pd})")
        mm = e("(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.))")
        rad = e("(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.))")
        sr = e("(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT())")
        unc = e(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),{mm},'distance_accuracy_value','confusion accuracy')")
        ctx = e(f"(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT(({unc})) "
                f"GLOBAL_UNIT_ASSIGNED_CONTEXT(({mm},{rad},{sr})) REPRESENTATION_CONTEXT('',''))")
        origin = e(f"AXIS2_PLACEMENT_3D('',{self._pt((0, 0, 0))},{self._dir((0, 0, 1))},{self._dir((1, 0, 0))})")
        rep = e(f"ADVANCED_BREP_SHAPE_REPRESENTATION('{self.name}',({origin}," +
                ",".join(s for s, _ in self.solids) + f"),{ctx})")
        e(f"SHAPE_DEFINITION_REPRESENTATION({pds},{rep})")
        styled = []
        for solid, (r, g, b) in self.solids:
            # one entity per line: Part 21 does not allow entity instances nested in parameters
            col = e(f"COLOUR_RGB('',{self._f(r)},{self._f(g)},{self._f(b)})")
            fasc = e(f"FILL_AREA_STYLE_COLOUR('',{col})")
            fill = e(f"FILL_AREA_STYLE('',({fasc}))")
            ssfa = e(f"SURFACE_STYLE_FILL_AREA({fill})")
            side = e(f"SURFACE_SIDE_STYLE('',({ssfa}))")
            usage = e(f"SURFACE_STYLE_USAGE(.BOTH.,{side})")
            psa = e(f"PRESENTATION_STYLE_ASSIGNMENT(({usage}))")
            styled.append(e(f"STYLED_ITEM('color',({psa}),{solid})"))
        e("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',(" + ",".join(styled) + f"),{ctx})")
        head = ("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('tredder 3D model'),'2;1');\n"
                f"FILE_NAME('{self.name}.step','2026-01-01T00:00:00',(''),(''),'tredder step_writer','tredder','');\n"
                "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n")
        body = "\n".join(f"#{i + 1}={t};" for i, t in enumerate(self.ents))
        with open(path, "w", encoding="ascii", newline="\n") as fh:
            fh.write(head + body + "\nENDSEC;\nEND-ISO-10303-21;\n")
