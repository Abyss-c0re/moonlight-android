package com.limelight.utils;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.RouteInfo;
import android.os.Build;

import com.limelight.LimeLog;

import java.net.InetAddress;

/**
 * The Titan keeps mobile data up next to Wi-Fi. Android then sources new
 * sockets from the cellular address, so a PC on the LAN never sees them.
 * Bind this process to the network that actually routes a private host.
 */
public final class LanNetwork {
    private LanNetwork() {}

    public static void bindForHost(Context context, String host) {
        if (context == null || host == null || host.isEmpty()) {
            return;
        }
        InetAddress dest;
        try {
            dest = InetAddress.getByName(host);
        } catch (Exception e) {
            return;
        }
        if (!(dest.isSiteLocalAddress() || dest.isLinkLocalAddress())) {
            return;
        }
        ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) {
            return;
        }
        Network best = null;
        boolean bestCellular = true;
        int bestPrefix = -1;
        Network[] networks = cm.getAllNetworks();
        if (networks == null) {
            return;
        }
        for (Network network : networks) {
            LinkProperties lp = cm.getLinkProperties(network);
            if (lp == null || lp.getRoutes() == null) {
                continue;
            }
            boolean cellular = isCellular(lp.getInterfaceName());
            for (RouteInfo route : lp.getRoutes()) {
                if (route == null || !route.matches(dest)) {
                    continue;
                }
                int prefix = prefixLength(route);
                boolean better = best == null
                        || (!cellular && bestCellular)
                        || (cellular == bestCellular && prefix > bestPrefix);
                if (better) {
                    best = network;
                    bestCellular = cellular;
                    bestPrefix = prefix;
                }
            }
        }
        if (best == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            cm.bindProcessToNetwork(best);
        } else {
            ConnectivityManager.setProcessDefaultNetwork(best);
        }
        LimeLog.info("LAN sockets for " + host + " use " + best);
    }

    private static boolean isCellular(String iface) {
        if (iface == null) {
            return false;
        }
        return iface.startsWith("ccmni")
                || iface.startsWith("rmnet")
                || iface.startsWith("pdp")
                || iface.startsWith("wwan");
    }

    private static int prefixLength(RouteInfo route) {
        try {
            if (route.getDestination() != null) {
                return route.getDestination().getPrefixLength();
            }
        } catch (Throwable ignored) {
        }
        return 0;
    }
}
